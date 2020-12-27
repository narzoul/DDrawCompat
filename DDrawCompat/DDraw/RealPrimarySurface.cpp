#include <atomic>
#include <memory>
#include <vector>

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
#include <Gdi/AccessGuard.h>
#include <Gdi/Caret.h>
#include <Gdi/Gdi.h>
#include <Gdi/VirtualScreen.h>
#include <Gdi/Window.h>
#include <Win32/DisplayMode.h>

namespace
{
	void onRelease();

	CompatWeakPtr<IDirectDrawSurface7> g_frontBuffer;
	CompatWeakPtr<IDirectDrawSurface7> g_paletteConverter;
	CompatWeakPtr<IDirectDrawClipper> g_clipper;
	DDSURFACEDESC2 g_surfaceDesc = {};
	DDraw::IReleaseNotifier g_releaseNotifier(onRelease);

	bool g_stopUpdateThread = false;
	HANDLE g_updateThread = nullptr;
	bool g_isFullScreen = false;
	DDraw::Surface* g_lastFlipSurface = nullptr;

	bool g_isUpdatePending = false;
	bool g_waitingForPrimaryUnlock = false;
	std::atomic<long long> g_qpcLastUpdate = 0;
	long long g_qpcFlipEnd = 0;
	UINT g_flipEndVsyncCount = 0;
	UINT g_presentEndVsyncCount = 0;

	CompatPtr<IDirectDrawSurface7> getBackBuffer();
	CompatPtr<IDirectDrawSurface7> getLastSurface();

	void bltToPrimaryChain(CompatRef<IDirectDrawSurface7> src)
	{
		if (!g_isFullScreen)
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

	template <typename TDirectDraw>
	HRESULT createPaletteConverter(CompatRef<TDirectDraw> dd)
	{
		auto dm = DDraw::getDisplayMode(*CompatPtr<IDirectDraw7>::from(&dd));
		if (dm.ddpfPixelFormat.dwRGBBitCount > 8)
		{
			return DD_OK;
		}

		typename DDraw::Types<TDirectDraw>::TSurfaceDesc desc = {};
		desc.dwSize = sizeof(desc);
		desc.dwFlags = DDSD_WIDTH | DDSD_HEIGHT | DDSD_PIXELFORMAT | DDSD_CAPS;
		desc.dwWidth = dm.dwWidth;
		desc.dwHeight = dm.dwHeight;
		desc.ddpfPixelFormat.dwSize = sizeof(desc.ddpfPixelFormat);
		desc.ddpfPixelFormat.dwFlags = DDPF_RGB;
		desc.ddpfPixelFormat.dwRGBBitCount = 32;
		desc.ddpfPixelFormat.dwRBitMask = 0x00FF0000;
		desc.ddpfPixelFormat.dwGBitMask = 0x0000FF00;
		desc.ddpfPixelFormat.dwBBitMask = 0x000000FF;
		desc.ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN | DDSCAPS_SYSTEMMEMORY;

		CompatPtr<DDraw::Types<TDirectDraw>::TCreatedSurface> paletteConverter;
		HRESULT result = dd->CreateSurface(&dd, &desc, &paletteConverter.getRef(), nullptr);
		if (SUCCEEDED(result))
		{
			g_paletteConverter = Compat::queryInterface<IDirectDrawSurface7>(paletteConverter.get());
		}

		return result;
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
		g_isFullScreen = false;
		g_waitingForPrimaryUnlock = false;
		g_paletteConverter.release();
		g_surfaceDesc = {};
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
		g_isFullScreen = isFlippable;
		g_isUpdatePending = false;
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

		RECT monitorRect = D3dDdi::KernelModeThunks::getMonitorRect();
		Gdi::Region excludeRegion(monitorRect);
		Gdi::Window::present(excludeRegion);

		if (Win32::DisplayMode::getBpp() <= 8)
		{
			HDC paletteConverterDc = nullptr;
			g_paletteConverter->GetDC(g_paletteConverter, &paletteConverterDc);
			HDC srcDc = nullptr;
			D3dDdi::Device::setReadOnlyGdiLock(true);
			D3dDdi::KernelModeThunks::setDcPaletteOverride(true);
			src->GetDC(src, &srcDc);
			D3dDdi::KernelModeThunks::setDcPaletteOverride(false);
			D3dDdi::Device::setReadOnlyGdiLock(false);

			if (paletteConverterDc && srcDc)
			{
				CALL_ORIG_FUNC(BitBlt)(paletteConverterDc,
					0, 0, g_surfaceDesc.dwWidth, g_surfaceDesc.dwHeight, srcDc, 0, 0, SRCCOPY);
			}

			src->ReleaseDC(src, srcDc);
			g_paletteConverter->ReleaseDC(g_paletteConverter, paletteConverterDc);

			bltToPrimaryChain(*g_paletteConverter);
		}
		else
		{
			bltToPrimaryChain(*src);
		}

		if (g_isFullScreen && src == DDraw::PrimarySurface::getGdiSurface())
		{
			auto backBuffer(getBackBuffer());
			if (backBuffer)
			{
				POINT offset = { -monitorRect.left, -monitorRect.top };
				Gdi::Window::presentLayered(*backBuffer, offset);
			}
		}
	}

	void updateNow(CompatWeakPtr<IDirectDrawSurface7> src, UINT flipInterval)
	{
		presentToPrimaryChain(src);
		g_isUpdatePending = false;
		g_waitingForPrimaryUnlock = false;

		if (g_isFullScreen)
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

	DWORD WINAPI updateThreadProc(LPVOID /*lpParameter*/)
	{
		bool skipWaitForVsync = false;

		while (!g_stopUpdateThread)
		{
			if (!skipWaitForVsync)
			{
				D3dDdi::KernelModeThunks::waitForVsync();
			}
			skipWaitForVsync = false;
			Gdi::Caret::blink();
			Sleep(1);

			DDraw::ScopedThreadLock lock;
			if (g_isUpdatePending && !isPresentPending())
			{
				if (Time::qpcToMs(Time::queryPerformanceCounter() - g_qpcFlipEnd) < 1)
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
		HRESULT result = createPaletteConverter(dd);
		if (FAILED(result))
		{
			Compat::Log() << "ERROR: Failed to create the palette converter surface: " << Compat::hex(result);
			return result;
		}

		typename Types<DirectDraw>::TSurfaceDesc desc = {};
		desc.dwSize = sizeof(desc);
		desc.dwFlags = DDSD_CAPS | DDSD_BACKBUFFERCOUNT;
		desc.ddsCaps.dwCaps = DDSCAPS_PRIMARYSURFACE | DDSCAPS_COMPLEX | DDSCAPS_FLIP;
		desc.dwBackBufferCount = 2;

		CompatPtr<typename Types<DirectDraw>::TCreatedSurface> surface;
		result = dd->CreateSurface(&dd, &desc, &surface.getRef(), nullptr);

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
			g_paletteConverter.release();
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
		g_presentEndVsyncCount = g_flipEndVsyncCount;

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

	CompatWeakPtr<IDirectDrawSurface7> RealPrimarySurface::getSurface()
	{
		return g_frontBuffer;
	}

	void RealPrimarySurface::init()
	{
		g_updateThread = CreateThread(nullptr, 0, &updateThreadProc, nullptr, 0, nullptr);
		SetThreadPriority(g_updateThread, THREAD_PRIORITY_TIME_CRITICAL);
	}

	bool RealPrimarySurface::isFullScreen()
	{
		return g_isFullScreen;
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

	void RealPrimarySurface::removeUpdateThread()
	{
		if (!g_updateThread)
		{
			return;
		}

		g_stopUpdateThread = true;
		if (WAIT_OBJECT_0 != WaitForSingleObject(g_updateThread, 1000))
		{
			TerminateThread(g_updateThread, 0);
			Compat::Log() << "The update thread was terminated forcefully";
		}
		g_updateThread = nullptr;
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

	bool RealPrimarySurface::waitForFlip(Surface* surface, bool wait)
	{
		auto primary(DDraw::PrimarySurface::getPrimary());
		if (!surface || !primary ||
			surface != g_lastFlipSurface &&
			surface != Surface::getSurface(*DDraw::PrimarySurface::getPrimary()))
		{
			return true;
		}

		if (!wait)
		{
			return !isFlipPending();
		}

		if (D3dDdi::KernelModeThunks::waitForVsyncCounter(g_flipEndVsyncCount))
		{
			g_qpcFlipEnd = Time::queryPerformanceCounter();
		}
		return true;
	}
}
