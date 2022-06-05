#include <memory>
#include <vector>

#include <Common/Comparison.h>
#include <Common/CompatPtr.h>
#include <Common/Hook.h>
#include <Common/ScopedCriticalSection.h>
#include <Common/Time.h>
#include <Config/Config.h>
#include <D3dDdi/Device.h>
#include <D3dDdi/KernelModeThunks.h>
#include <D3dDdi/Resource.h>
#include <D3dDdi/ScopedCriticalSection.h>
#include <D3dDdi/SurfaceRepository.h>
#include <DDraw/DirectDraw.h>
#include <DDraw/DirectDrawSurface.h>
#include <DDraw/IReleaseNotifier.h>
#include <DDraw/RealPrimarySurface.h>
#include <DDraw/ScopedThreadLock.h>
#include <DDraw/Surfaces/PrimarySurface.h>
#include <DDraw/Surfaces/TagSurface.h>
#include <DDraw/Types.h>
#include <Gdi/Caret.h>
#include <Gdi/Cursor.h>
#include <Gdi/Gdi.h>
#include <Gdi/GuiThread.h>
#include <Gdi/Palette.h>
#include <Gdi/VirtualScreen.h>
#include <Gdi/Window.h>
#include <Gdi/WinProc.h>
#include <Overlay/ConfigWindow.h>
#include <Win32/DisplayMode.h>

namespace
{
	const unsigned DELAYED_FLIP_MODE_TIMEOUT_MS = 200;

	void onRelease();

	CompatWeakPtr<IDirectDrawSurface7> g_frontBuffer;
	CompatWeakPtr<IDirectDrawSurface7> g_windowedBackBuffer;
	CompatWeakPtr<IDirectDrawClipper> g_clipper;
	RECT g_monitorRect = {};
	DDSURFACEDESC2 g_surfaceDesc = {};
	DDraw::IReleaseNotifier g_releaseNotifier(onRelease);

	bool g_isFullscreen = false;
	DDraw::Surface* g_lastFlipSurface = nullptr;

	Compat::CriticalSection g_presentCs;
	bool g_isDelayedFlipPending = false;
	bool g_isUpdatePending = false;
	bool g_isUpdateReady = false;
	DWORD g_lastUpdateThreadId = 0;
	long long g_qpcLastUpdate = 0;
	long long g_qpcUpdateStart = 0;

	long long g_qpcDelayedFlipEnd = 0;
	UINT g_flipEndVsyncCount = 0;
	UINT g_presentEndVsyncCount = 0;

	HWND g_devicePresentationWindow = nullptr;
	HWND g_deviceWindow = nullptr;
	HWND* g_deviceWindowPtr = nullptr;

	CompatPtr<IDirectDrawSurface7> getBackBuffer();
	CompatPtr<IDirectDrawSurface7> getLastSurface();

	void bltToPrimaryChain(CompatRef<IDirectDrawSurface7> src)
	{
		if (!g_isFullscreen)
		{
			{
				D3dDdi::ScopedCriticalSection lock;
				auto srcResource = D3dDdi::Device::findResource(
					DDraw::DirectDrawSurface::getDriverResourceHandle(src.get()));
				auto bbResource = D3dDdi::Device::findResource(
					DDraw::DirectDrawSurface::getDriverResourceHandle(*g_windowedBackBuffer));

				D3DDDIARG_BLT blt = {};
				blt.hSrcResource = *srcResource;
				blt.SrcSubResourceIndex = DDraw::DirectDrawSurface::getSubResourceIndex(src.get());
				blt.SrcRect = DDraw::PrimarySurface::getMonitorRect();
				blt.hDstResource = *bbResource;
				blt.DstSubResourceIndex = 0;
				blt.DstRect = g_monitorRect;
				bbResource->presentationBlt(blt, srcResource);
			}

			Gdi::Window::present(*g_frontBuffer, *g_windowedBackBuffer, *g_clipper);
			return;
		}

		auto backBuffer(getBackBuffer());
		if (backBuffer)
		{
			backBuffer->Blt(backBuffer, nullptr, &src, nullptr, DDBLT_WAIT, nullptr);
		}
	}

	CompatPtr<IDirectDrawSurface7> createWindowedBackBuffer(DDRAWI_DIRECTDRAW_LCL* ddLcl, DWORD width, DWORD height)
	{
		if (!ddLcl)
		{
			LOG_INFO << "ERROR: createWindowedBackBuffer: ddLcl is null";
			return nullptr;
		}

		auto tagSurface = DDraw::TagSurface::get(ddLcl);
		if (!tagSurface)
		{
			LOG_INFO << "ERROR: createWindowedBackBuffer: TagSurface not found";
			return nullptr;
		}

		auto resource = DDraw::DirectDrawSurface::getDriverResourceHandle(*tagSurface->getDDS());
		if (!resource)
		{
			LOG_INFO << "ERROR: createWindowedBackBuffer: driver resource handle not found";
			return nullptr;
		}

		auto device = D3dDdi::Device::findDeviceByResource(resource);
		if (!device)
		{
			LOG_INFO << "ERROR: createWindowedBackBuffer: device not found";
			return nullptr;
		}

		auto& repo = D3dDdi::SurfaceRepository::get(device->getAdapter());
		D3dDdi::SurfaceRepository::Surface surface = {};
		repo.getSurface(surface, width, height, DDraw::DirectDraw::getRgbPixelFormat(32),
			DDSCAPS_OFFSCREENPLAIN | DDSCAPS_3DDEVICE | DDSCAPS_VIDEOMEMORY);
		if (!surface.surface)
		{
			LOG_INFO << "ERROR: createWindowedBackBuffer: surface creation failed";
			return nullptr;
		}

		return surface.surface;
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
		auto vSync = Config::vSync.get();
		if (Config::Settings::VSync::APP != vSync)
		{
			return Config::Settings::VSync::OFF == vSync ? 0 : Config::vSync.getParam();
		}

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

	void onRelease()
	{
		LOG_FUNC("RealPrimarySurface::onRelease");

		g_frontBuffer = nullptr;
		g_lastFlipSurface = nullptr;
		g_windowedBackBuffer.release();
		g_clipper.release();
		g_isFullscreen = false;
		g_surfaceDesc = {};

		DDraw::RealPrimarySurface::updateDevicePresentationWindowPos();
		g_devicePresentationWindow = nullptr;
		g_deviceWindow = nullptr;
		g_deviceWindowPtr = nullptr;
		g_monitorRect = {};
	}

	void onRestore()
	{
		DDSURFACEDESC2 desc = {};
		desc.dwSize = sizeof(desc);
		g_frontBuffer->GetSurfaceDesc(g_frontBuffer, &desc);

		g_clipper.release();
		CALL_ORIG_PROC(DirectDrawCreateClipper)(0, &g_clipper.getRef(), nullptr);
		g_frontBuffer->SetClipper(g_frontBuffer, g_clipper);

		const bool isFlippable = 0 != (desc.ddsCaps.dwCaps & DDSCAPS_FLIP);
		g_surfaceDesc = desc;
		g_isFullscreen = isFlippable;

		if (isFlippable)
		{
			g_frontBuffer->Flip(g_frontBuffer, getLastSurface(), DDFLIP_WAIT);
			D3dDdi::KernelModeThunks::waitForVsyncCounter(D3dDdi::KernelModeThunks::getVsyncCounter() + 1);
		}

		if (g_windowedBackBuffer)
		{
			g_windowedBackBuffer->Restore(g_windowedBackBuffer);
		}

		Compat::ScopedCriticalSection lock(g_presentCs);
		g_isUpdatePending = false;
		g_isUpdateReady = false;
		g_qpcLastUpdate = Time::queryPerformanceCounter() - Time::msToQpc(DELAYED_FLIP_MODE_TIMEOUT_MS);
		g_qpcUpdateStart = g_qpcLastUpdate;
		g_presentEndVsyncCount = D3dDdi::KernelModeThunks::getVsyncCounter();
		g_flipEndVsyncCount = g_presentEndVsyncCount;
	}

	void presentToPrimaryChain(CompatWeakPtr<IDirectDrawSurface7> src)
	{
		LOG_FUNC("RealPrimarySurface::presentToPrimaryChain", src);

		Gdi::VirtualScreen::update();

		Gdi::GuiThread::execute([]()
			{
				auto configWindow = Gdi::GuiThread::getConfigWindow();
				if (configWindow)
				{
					configWindow->update();
				}

				auto capture = Input::getCaptureWindow();
				if (capture)
				{
					capture->update();
				}

				Input::updateCursor();
			});

		if (!g_frontBuffer || !src || DDraw::RealPrimarySurface::isLost())
		{
			Gdi::Window::present(nullptr);
			return;
		}

		Gdi::Region excludeRegion(DDraw::PrimarySurface::getMonitorRect());
		Gdi::Window::present(excludeRegion);
		bltToPrimaryChain(*src);
	}

	void updateNow(CompatWeakPtr<IDirectDrawSurface7> src)
	{
		{
			Compat::ScopedCriticalSection lock(g_presentCs);
			g_isUpdatePending = false;
			g_isUpdateReady = false;
		}

		presentToPrimaryChain(src);

		if (g_isFullscreen && g_devicePresentationWindow)
		{
			*g_deviceWindowPtr = g_devicePresentationWindow;
			g_frontBuffer->Flip(g_frontBuffer, getBackBuffer(), DDFLIP_WAIT);
			*g_deviceWindowPtr = g_deviceWindow;
		}
		g_presentEndVsyncCount = D3dDdi::KernelModeThunks::getVsyncCounter() + 1;
	}

	unsigned WINAPI updateThreadProc(LPVOID /*lpParameter*/)
	{
		int msUntilUpdateReady = 0;
		while (true)
		{
			if (msUntilUpdateReady > 0)
			{
				Sleep(1);
			}
			else
			{
				D3dDdi::KernelModeThunks::waitForVsyncCounter(D3dDdi::KernelModeThunks::getVsyncCounter() + 1);
			}

			DDraw::ScopedThreadLock lock;
			msUntilUpdateReady = DDraw::RealPrimarySurface::flush();
		}
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
			LOG_INFO << "ERROR: Failed to create the real primary surface: " << Compat::hex(result);
			g_monitorRect = {};
			return result;
		}

		if (0 == desc.dwBackBufferCount)
		{
			g_windowedBackBuffer = createWindowedBackBuffer(DDraw::DirectDraw::getInt(dd.get()).lpLcl,
				g_monitorRect.right - g_monitorRect.left, g_monitorRect.bottom - g_monitorRect.top).detach();
			if (!g_windowedBackBuffer)
			{
				g_monitorRect = {};
				return DDERR_GENERIC;
			}
		}

		g_frontBuffer = CompatPtr<IDirectDrawSurface7>::from(surface.get()).detach();
		g_frontBuffer->SetPrivateData(g_frontBuffer, IID_IReleaseNotifier,
			&g_releaseNotifier, sizeof(&g_releaseNotifier), DDSPD_IUNKNOWNPOINTER);
		
		g_deviceWindowPtr = DDraw::DirectDraw::getDeviceWindowPtr(dd.get());
		g_deviceWindow = g_deviceWindowPtr ? *g_deviceWindowPtr : nullptr;
		g_devicePresentationWindow = Gdi::Window::getPresentationWindow(g_deviceWindow);

		onRestore();
		updateDevicePresentationWindowPos();

		return DD_OK;
	}

	template HRESULT RealPrimarySurface::create(CompatRef<IDirectDraw>);
	template HRESULT RealPrimarySurface::create(CompatRef<IDirectDraw2>);
	template HRESULT RealPrimarySurface::create(CompatRef<IDirectDraw4>);
	template HRESULT RealPrimarySurface::create(CompatRef<IDirectDraw7>);

	HRESULT RealPrimarySurface::flip(CompatPtr<IDirectDrawSurface7> surfaceTargetOverride, DWORD flags)
	{
		const DWORD flipInterval = getFlipInterval(flags);
		if (0 == flipInterval ||
			Time::qpcToMs(Time::queryPerformanceCounter() - g_qpcLastUpdate) < DELAYED_FLIP_MODE_TIMEOUT_MS)
		{
			PrimarySurface::waitForIdle();
			Compat::ScopedCriticalSection lock(g_presentCs);
			g_isDelayedFlipPending = true;
			g_isUpdatePending = false;
			g_isUpdateReady = false;
			g_lastUpdateThreadId = GetCurrentThreadId();
		}
		else
		{
			updateNow(PrimarySurface::getPrimary());
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

		g_qpcDelayedFlipEnd = Time::queryPerformanceCounter();
		return DD_OK;
	}

	int RealPrimarySurface::flush()
	{
		auto vsyncCount = D3dDdi::KernelModeThunks::getVsyncCounter();
		if (static_cast<int>(vsyncCount - g_presentEndVsyncCount) < 0)
		{
			return -1;
		}

		{
			Compat::ScopedCriticalSection lock(g_presentCs);
			if (!g_isUpdateReady)
			{
				if (g_isUpdatePending)
				{
					auto msSinceUpdateStart = Time::qpcToMs(Time::queryPerformanceCounter() - g_qpcUpdateStart);
					if (msSinceUpdateStart < 10)
					{
						return 10 - static_cast<int>(msSinceUpdateStart);
					}
					g_isUpdateReady = true;
				}
				else if (g_isDelayedFlipPending)
				{
					auto msSinceDelayedFlipEnd = Time::qpcToMs(Time::queryPerformanceCounter() - g_qpcDelayedFlipEnd);
					if (msSinceDelayedFlipEnd < 3)
					{
						return 3 - static_cast<int>(msSinceDelayedFlipEnd);
					}
					g_isDelayedFlipPending = false;
					g_isUpdateReady = true;
				}
			}

			if (!g_isUpdateReady)
			{
				return -1;
			}
		}

		auto src(g_isDelayedFlipPending ? g_lastFlipSurface->getDDS() : DDraw::PrimarySurface::getPrimary());
		RECT emptyRect = {};
		HRESULT result = src ? src->BltFast(src, 0, 0, src, &emptyRect, DDBLTFAST_WAIT) : DD_OK;
		if (DDERR_SURFACEBUSY == result || DDERR_LOCKEDSURFACES == result)
		{
			return 1;
		}

		updateNow(src);
		return 0;
	}

	HWND RealPrimarySurface::getDevicePresentationWindow()
	{
		return g_devicePresentationWindow;
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

	HWND RealPrimarySurface::getTopmost()
	{
		if (g_isFullscreen && g_devicePresentationWindow)
		{
			return g_devicePresentationWindow;
		}
		return HWND_TOPMOST;
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
		Compat::ScopedCriticalSection lock(g_presentCs);
		g_qpcLastUpdate = Time::queryPerformanceCounter();
		if (!g_isUpdatePending)
		{
			g_qpcUpdateStart = g_qpcLastUpdate;
			g_isUpdatePending = true;
			g_isDelayedFlipPending = false;
			g_lastUpdateThreadId = GetCurrentThreadId();
		}
		g_isUpdateReady = false;
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

	void RealPrimarySurface::setUpdateReady()
	{
		Compat::ScopedCriticalSection lock(g_presentCs);
		if ((g_isUpdatePending || g_isDelayedFlipPending) && GetCurrentThreadId() == g_lastUpdateThreadId)
		{
			g_isUpdateReady = true;
			g_isDelayedFlipPending = false;
		}
	}

	void RealPrimarySurface::updateDevicePresentationWindowPos()
	{
		if (!g_devicePresentationWindow)
		{
			return;
		}

		Gdi::GuiThread::execute([&]()
			{
				if (g_isFullscreen && IsWindowVisible(g_deviceWindow) && !IsIconic(g_deviceWindow))
				{
					CALL_ORIG_FUNC(SetWindowPos)(g_devicePresentationWindow, HWND_TOPMOST, g_monitorRect.left, g_monitorRect.top,
						g_monitorRect.right - g_monitorRect.left, g_monitorRect.bottom - g_monitorRect.top,
						SWP_NOACTIVATE | SWP_NOSENDCHANGING | SWP_NOREDRAW | SWP_NOOWNERZORDER | SWP_SHOWWINDOW);
				}
				else
				{
					Gdi::Window::updatePresentationWindowPos(g_devicePresentationWindow, g_deviceWindow);
				}
			});
	}

	bool RealPrimarySurface::waitForFlip(CompatWeakPtr<IDirectDrawSurface7> surface)
	{
		auto primary(DDraw::PrimarySurface::getPrimary());
		if (!surface || !primary || !g_lastFlipSurface ||
			surface != primary && surface != g_lastFlipSurface->getDDS())
		{
			return true;
		}

		auto vsyncCount = D3dDdi::KernelModeThunks::getVsyncCounter();
		while (static_cast<int>(vsyncCount - g_flipEndVsyncCount) < 0)
		{
			flush();
			++vsyncCount;
			D3dDdi::KernelModeThunks::waitForVsyncCounter(vsyncCount);
			g_qpcDelayedFlipEnd = Time::queryPerformanceCounter();
		}
		return true;
	}
}
