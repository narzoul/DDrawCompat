#include <atomic>
#include <memory>
#include <vector>

#include <Common/Comparison.h>
#include <Common/CompatPtr.h>
#include <Common/Hook.h>
#include <Common/Time.h>
#include <Config/Config.h>
#include <D3dDdi/Device.h>
#include <D3dDdi/KernelModeThunks.h>
#include <DDraw/DirectDraw.h>
#include <DDraw/DirectDrawSurface.h>
#include <DDraw/IReleaseNotifier.h>
#include <DDraw/RealPrimarySurface.h>
#include <DDraw/ScopedThreadLock.h>
#include <DDraw/Surfaces/PrimarySurface.h>
#include <DDraw/Types.h>
#include <Gdi/Caret.h>
#include <Gdi/Cursor.h>
#include <Gdi/Gdi.h>
#include <Gdi/Palette.h>
#include <Gdi/VirtualScreen.h>
#include <Gdi/Window.h>
#include <Gdi/WinProc.h>
#include <Win32/DisplayMode.h>

namespace
{
	void onRelease();

	CompatWeakPtr<IDirectDrawSurface7> g_frontBuffer;
	CompatWeakPtr<IDirectDrawClipper> g_clipper;
	RECT g_monitorRect = {};
	DDSURFACEDESC2 g_surfaceDesc = {};
	DDraw::IReleaseNotifier g_releaseNotifier(onRelease);

	bool g_isFullscreen = false;
	DDraw::Surface* g_lastFlipSurface = nullptr;

	bool g_isUpdatePending = false;
	bool g_waitingForPrimaryUnlock = false;
	std::atomic<long long> g_qpcLastUpdate = 0;
	UINT g_flipEndVsyncCount = 0;
	UINT g_presentEndVsyncCount = 0;

	CompatPtr<IDirectDrawSurface7> getBackBuffer();
	CompatPtr<IDirectDrawSurface7> getLastSurface();

	void bltToPrimaryChain(CompatRef<IDirectDrawSurface7> src)
	{
		if (!g_isFullscreen)
		{
			Gdi::Window::present(*g_frontBuffer, src, *g_clipper);
			return;
		}

		auto backBuffer(getBackBuffer());
		if (backBuffer)
		{
			backBuffer->Blt(backBuffer, nullptr, &src, nullptr, DDBLT_WAIT, nullptr);
		}
	}

	CompatPtr<IDirectDrawSurface7> getBackBuffer()
	{
		DDSCAPS2 caps = {};
		caps.dwCaps = DDSCAPS_BACKBUFFER;
		CompatPtr<IDirectDrawSurface7> backBuffer;
		if (g_frontBuffer)
		{
			g_frontBuffer->GetAttachedSurface(g_frontBuffer, &caps, &backBuffer.getRef());
		}
		return backBuffer;
	}

	CompatPtr<IDirectDrawSurface7> getLastSurface()
	{
		DDSCAPS2 caps = {};
		caps.dwCaps = DDSCAPS_FLIP;;
		CompatPtr<IDirectDrawSurface7> backBuffer(getBackBuffer());
		CompatPtr<IDirectDrawSurface7> lastSurface;
		if (backBuffer)
		{
			backBuffer->GetAttachedSurface(backBuffer, &caps, &lastSurface.getRef());
		}
		return lastSurface;
	}

	UINT getFlipInterval(DWORD flags)
	{
		if (flags & DDFLIP_NOVSYNC)
		{
			return 0;
		}

		if (flags & (DDFLIP_INTERVAL2 | DDFLIP_INTERVAL3 | DDFLIP_INTERVAL4))
		{
			UINT flipInterval = (flags & (DDFLIP_INTERVAL2 | DDFLIP_INTERVAL3 | DDFLIP_INTERVAL4)) >> 24;
			if (flipInterval < 2 || flipInterval > 4)
			{
				flipInterval = 1;
			}
			return flipInterval;
		}

		return 1;
	}

	bool isFlipPending()
	{
		return static_cast<INT>(D3dDdi::KernelModeThunks::getVsyncCounter() - g_flipEndVsyncCount) < 0;
	}

	bool isPresentPending()
	{
		return static_cast<INT>(D3dDdi::KernelModeThunks::getVsyncCounter() - g_presentEndVsyncCount) < 0;
	}

	void onRelease()
	{
		LOG_FUNC("RealPrimarySurface::onRelease");

		g_frontBuffer = nullptr;
		g_clipper.release();
		g_isFullscreen = false;
		g_waitingForPrimaryUnlock = false;
		g_surfaceDesc = {};
		g_monitorRect = {};
	}

	void onRestore()
	{
		DDSURFACEDESC2 desc = {};
		desc.dwSize = sizeof(desc);
		g_frontBuffer->GetSurfaceDesc(g_frontBuffer, &desc);

		g_clipper.release();

		const bool isFlippable = 0 != (desc.ddsCaps.dwCaps & DDSCAPS_FLIP);
		if (!isFlippable)
		{
			CALL_ORIG_PROC(DirectDrawCreateClipper)(0, &g_clipper.getRef(), nullptr);
			g_frontBuffer->SetClipper(g_frontBuffer, g_clipper);
		}

		g_surfaceDesc = desc;
		g_isFullscreen = isFlippable;
		g_isUpdatePending = true;
		g_qpcLastUpdate = Time::queryPerformanceCounter() - Time::msToQpc(Config::delayedFlipModeTimeout);

		if (isFlippable)
		{
			g_frontBuffer->Flip(g_frontBuffer, getLastSurface(), DDFLIP_WAIT);
			g_flipEndVsyncCount = D3dDdi::KernelModeThunks::getVsyncCounter() + 1;
			g_presentEndVsyncCount = g_flipEndVsyncCount;
			D3dDdi::KernelModeThunks::waitForVsyncCounter(g_flipEndVsyncCount);
		}
	}

	void presentToPrimaryChain(CompatWeakPtr<IDirectDrawSurface7> src)
	{
		LOG_FUNC("RealPrimarySurface::presentToPrimaryChain", src);

		Gdi::VirtualScreen::update();

		if (!g_frontBuffer || !src || DDraw::RealPrimarySurface::isLost())
		{
			Gdi::Window::present(nullptr);
			return;
		}

		Gdi::Region excludeRegion(DDraw::PrimarySurface::getMonitorRect());
		Gdi::Window::present(excludeRegion);

		auto palette(Gdi::Palette::getHardwarePalette());
		D3dDdi::KernelModeThunks::setDcPaletteOverride(palette.data());
		bltToPrimaryChain(*src);
		D3dDdi::KernelModeThunks::setDcPaletteOverride(nullptr);
	}

	void updateNow(CompatWeakPtr<IDirectDrawSurface7> src, UINT flipInterval)
	{
		presentToPrimaryChain(src);
		g_isUpdatePending = false;
		g_waitingForPrimaryUnlock = false;

		if (g_isFullscreen)
		{
			g_frontBuffer->Flip(g_frontBuffer, getBackBuffer(), DDFLIP_WAIT);
		}
		g_presentEndVsyncCount = D3dDdi::KernelModeThunks::getVsyncCounter() + max(flipInterval, 1);
	}

	void updateNowIfNotBusy()
	{
		auto primary(DDraw::PrimarySurface::getPrimary());
		RECT emptyRect = {};
		HRESULT result = primary ? primary->BltFast(primary, 0, 0, primary, &emptyRect, DDBLTFAST_WAIT) : DD_OK;
		g_waitingForPrimaryUnlock = DDERR_SURFACEBUSY == result || DDERR_LOCKEDSURFACES == result;

		if (!g_waitingForPrimaryUnlock)
		{
			const auto msSinceLastUpdate = Time::qpcToMs(Time::queryPerformanceCounter() - g_qpcLastUpdate);
			updateNow(primary, msSinceLastUpdate > Config::delayedFlipModeTimeout ? 0 : 1);
		}
	}

	unsigned WINAPI updateThreadProc(LPVOID /*lpParameter*/)
	{
		bool skipWaitForVsync = false;

		while (true)
		{
			if (!skipWaitForVsync)
			{
				D3dDdi::KernelModeThunks::waitForVsync();
			}
			skipWaitForVsync = false;
			Sleep(1);

			DDraw::ScopedThreadLock lock;
			Gdi::Caret::blink();
			if (Gdi::Cursor::update())
			{
				g_isUpdatePending = true;
			}

			if (g_isUpdatePending && !isPresentPending())
			{
				auto qpcNow = Time::queryPerformanceCounter();
				auto qpcLastVsync = D3dDdi::KernelModeThunks::getQpcLastVsync();
				if (Time::qpcToMs(qpcNow - qpcLastVsync) < 1 ||
					Time::qpcToMs(qpcNow - g_qpcLastUpdate) < 1 && Time::qpcToMs(qpcNow - qpcLastVsync) <= 3)
				{
					skipWaitForVsync = true;
				}
				else
				{
					updateNowIfNotBusy();
				}
			}
		}

		return 0;
	}
}

namespace DDraw
{
	template <typename DirectDraw>
	HRESULT RealPrimarySurface::create(CompatRef<DirectDraw> dd)
	{
		DDraw::ScopedThreadLock lock;
		g_monitorRect = D3dDdi::KernelModeThunks::getAdapterInfo(*CompatPtr<IDirectDraw7>::from(&dd)).monitorInfo.rcMonitor;

		typename Types<DirectDraw>::TSurfaceDesc desc = {};
		desc.dwSize = sizeof(desc);
		desc.dwFlags = DDSD_CAPS | DDSD_BACKBUFFERCOUNT;
		desc.ddsCaps.dwCaps = DDSCAPS_PRIMARYSURFACE | DDSCAPS_3DDEVICE | DDSCAPS_COMPLEX | DDSCAPS_FLIP;
		desc.dwBackBufferCount = 2;

		CompatPtr<typename Types<DirectDraw>::TCreatedSurface> surface;
		HRESULT result = dd->CreateSurface(&dd, &desc, &surface.getRef(), nullptr);

		if (DDERR_NOEXCLUSIVEMODE == result)
		{
			desc.dwFlags = DDSD_CAPS;
			desc.ddsCaps.dwCaps = DDSCAPS_PRIMARYSURFACE;
			desc.dwBackBufferCount = 0;
			result = dd->CreateSurface(&dd, &desc, &surface.getRef(), nullptr);
		}

		if (FAILED(result))
		{
			Compat::Log() << "ERROR: Failed to create the real primary surface: " << Compat::hex(result);
			g_monitorRect = {};
			return result;
		}

		g_frontBuffer = CompatPtr<IDirectDrawSurface7>::from(surface.get()).detach();
		g_frontBuffer->SetPrivateData(g_frontBuffer, IID_IReleaseNotifier,
			&g_releaseNotifier, sizeof(&g_releaseNotifier), DDSPD_IUNKNOWNPOINTER);
		onRestore();

		return DD_OK;
	}

	template HRESULT RealPrimarySurface::create(CompatRef<IDirectDraw>);
	template HRESULT RealPrimarySurface::create(CompatRef<IDirectDraw2>);
	template HRESULT RealPrimarySurface::create(CompatRef<IDirectDraw4>);
	template HRESULT RealPrimarySurface::create(CompatRef<IDirectDraw7>);

	HRESULT RealPrimarySurface::flip(CompatPtr<IDirectDrawSurface7> surfaceTargetOverride, DWORD flags)
	{
		const DWORD flipInterval = getFlipInterval(flags);
		if (0 == flipInterval)
		{
			g_isUpdatePending = true;
			return DD_OK;
		}

		const auto msSinceLastUpdate = Time::qpcToMs(Time::queryPerformanceCounter() - g_qpcLastUpdate);
		const bool isFlipDelayed = msSinceLastUpdate >= 0 && msSinceLastUpdate <= Config::delayedFlipModeTimeout;
		if (isFlipDelayed)
		{
			if (!isPresentPending())
			{
				CompatPtr<IDirectDrawSurface7> prevPrimarySurface(
					surfaceTargetOverride ? surfaceTargetOverride : PrimarySurface::getLastSurface());
				updateNow(prevPrimarySurface, 0);
			}
			g_isUpdatePending = true;
		}
		else
		{
			updateNow(PrimarySurface::getPrimary(), flipInterval);
		}
		g_flipEndVsyncCount = D3dDdi::KernelModeThunks::getVsyncCounter() + flipInterval;

		if (0 != flipInterval)
		{
			g_lastFlipSurface = Surface::getSurface(
				surfaceTargetOverride ? *surfaceTargetOverride : *PrimarySurface::getLastSurface());
		}
		else
		{
			g_lastFlipSurface = nullptr;
		}
		return DD_OK;
	}

	void RealPrimarySurface::flush()
	{
		DDraw::ScopedThreadLock lock;
		if (g_isUpdatePending && !isPresentPending())
		{
			updateNowIfNotBusy();
		}
	}

	HRESULT RealPrimarySurface::getGammaRamp(DDGAMMARAMP* rampData)
	{
		DDraw::ScopedThreadLock lock;
		auto gammaControl(CompatPtr<IDirectDrawGammaControl>::from(g_frontBuffer.get()));
		if (!gammaControl)
		{
			return DDERR_INVALIDPARAMS;
		}

		return gammaControl->GetGammaRamp(gammaControl, 0, rampData);
	}

	RECT RealPrimarySurface::getMonitorRect()
	{
		return g_monitorRect;
	}

	CompatWeakPtr<IDirectDrawSurface7> RealPrimarySurface::getSurface()
	{
		return g_frontBuffer;
	}

	void RealPrimarySurface::init()
	{
		Dll::createThread(&updateThreadProc, nullptr, THREAD_PRIORITY_TIME_CRITICAL);
	}

	bool RealPrimarySurface::isFullscreen()
	{
		return g_isFullscreen;
	}

	bool RealPrimarySurface::isLost()
	{
		DDraw::ScopedThreadLock lock;
		return g_frontBuffer && DDERR_SURFACELOST == g_frontBuffer->IsLost(g_frontBuffer);
	}

	void RealPrimarySurface::release()
	{
		DDraw::ScopedThreadLock lock;
		g_frontBuffer.release();
	}

	HRESULT RealPrimarySurface::restore()
	{
		DDraw::ScopedThreadLock lock;
		HRESULT result = g_frontBuffer->Restore(g_frontBuffer);
		if (SUCCEEDED(result))
		{
			onRestore();
		}
		return result;
	}

	void RealPrimarySurface::scheduleUpdate()
	{
		g_qpcLastUpdate = Time::queryPerformanceCounter();
		g_isUpdatePending = true;
	}

	HRESULT RealPrimarySurface::setGammaRamp(DDGAMMARAMP* rampData)
	{
		DDraw::ScopedThreadLock lock;
		auto gammaControl(CompatPtr<IDirectDrawGammaControl>::from(g_frontBuffer.get()));
		if (!gammaControl)
		{
			return DDERR_INVALIDPARAMS;
		}

		return gammaControl->SetGammaRamp(gammaControl, 0, rampData);
	}

	void RealPrimarySurface::update()
	{
		DDraw::ScopedThreadLock lock;
		g_qpcLastUpdate = Time::queryPerformanceCounter();
		g_isUpdatePending = true;
		if (g_waitingForPrimaryUnlock)
		{
			updateNowIfNotBusy();
		}
	}

	bool RealPrimarySurface::waitForFlip(Surface* surface)
	{
		auto primary(DDraw::PrimarySurface::getPrimary());
		if (!surface || !primary ||
			surface != g_lastFlipSurface &&
			surface != Surface::getSurface(*DDraw::PrimarySurface::getPrimary()))
		{
			return true;
		}

		D3dDdi::KernelModeThunks::waitForVsyncCounter(g_flipEndVsyncCount);
		return true;
	}
}
