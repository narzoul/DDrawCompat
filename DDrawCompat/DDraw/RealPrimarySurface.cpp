#include <memory>
#include <vector>

#include <Windows.h>
#include <VersionHelpers.h>

#include <Common/Comparison.h>
#include <Common/CompatPtr.h>
#include <Common/Hook.h>
#include <Common/ScopedCriticalSection.h>
#include <Common/ScopedThreadPriority.h>
#include <Common/Time.h>
#include <Config/Settings/FpsLimiter.h>
#include <Config/Settings/FullscreenMode.h>
#include <Config/Settings/VSync.h>
#include <D3dDdi/Device.h>
#include <D3dDdi/KernelModeThunks.h>
#include <D3dDdi/Resource.h>
#include <D3dDdi/ScopedCriticalSection.h>
#include <D3dDdi/SurfaceRepository.h>
#include <DDraw/DirectDraw.h>
#include <DDraw/DirectDrawSurface.h>
#include <DDraw/IReleaseNotifier.h>
#include <DDraw/LogUsedResourceFormat.h>
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
#include <Gdi/PresentationWindow.h>
#include <Gdi/VirtualScreen.h>
#include <Gdi/Window.h>
#include <Gdi/WinProc.h>
#include <Overlay/ConfigWindow.h>
#include <Overlay/StatsWindow.h>
#include <Overlay/Steam.h>
#include <Win32/DisplayMode.h>
#include <Win32/DpiAwareness.h>
#include <Win32/Thread.h>

namespace
{
	const unsigned DELAYED_FLIP_MODE_TIMEOUT_MS = 200;

	void onRelease();
	void updatePresentationParams();

	CompatWeakPtr<IDirectDrawSurface7> g_frontBuffer;
	CompatWeakPtr<IDirectDrawSurface7> g_windowedBackBuffer;
	CompatWeakPtr<IDirectDrawClipper> g_clipper;
	RECT g_monitorRect = {};
	DDraw::IReleaseNotifier g_releaseNotifier(onRelease);

	bool g_isFullscreen = false;
	bool g_isExclusiveFullscreen = false;
	DDraw::Surface* g_lastFlipSurface = nullptr;
	DDraw::TagSurface* g_tagSurface = nullptr;

	Compat::CriticalSection g_presentCs;
	bool g_isDelayedFlipPending = false;
	bool g_isOverlayUpdatePending = false;
	bool g_isUpdatePending = false;
	bool g_isUpdateReady = false;
	DWORD g_lastUpdateThreadId = 0;
	long long g_qpcLastUpdate = 0;
	long long g_qpcUpdateStart = 0;

	long long g_qpcDelayedFlipEnd = 0;
	UINT g_flipEndVsyncCount = 0;
	UINT g_presentEndVsyncCount = 0;

	HWND g_deviceWindow = nullptr;
	HWND* g_deviceWindowPtr = nullptr;
	HWND g_presentationWindow = nullptr;

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
		caps.dwCaps = DDSCAPS_FLIP;
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

	bool isProcessActive()
	{
		const HWND foregroundWindow = GetForegroundWindow();
		if (foregroundWindow)
		{
			DWORD pid = 0;
			GetWindowThreadProcessId(foregroundWindow, &pid);
			return GetCurrentProcessId() == pid;
		}
		return false;
	}

	void onRelease()
	{
		LOG_FUNC("RealPrimarySurface::onRelease");

		g_frontBuffer = nullptr;
		g_lastFlipSurface = nullptr;
		g_windowedBackBuffer.release();
		g_isFullscreen = false;
		g_tagSurface = nullptr;

		g_deviceWindow = nullptr;
		g_deviceWindowPtr = nullptr;
	}

	void onRestore()
	{
		DDSURFACEDESC2 desc = {};
		desc.dwSize = sizeof(desc);
		g_frontBuffer->GetSurfaceDesc(g_frontBuffer, &desc);

		if (g_isExclusiveFullscreen && 0 != (desc.ddsCaps.dwCaps & DDSCAPS_FLIP))
		{
			g_frontBuffer->Flip(g_frontBuffer, getLastSurface(), DDFLIP_WAIT);
			D3dDdi::KernelModeThunks::waitForVsyncCounter(D3dDdi::KernelModeThunks::getVsyncCounter() + 1);
		}

		auto gdiResource = DDraw::PrimarySurface::getGdiResource();
		if (gdiResource)
		{
			D3dDdi::Device::setGdiResourceHandle(gdiResource);
		}

		updatePresentationParams();

		Compat::ScopedCriticalSection lock(g_presentCs);
		g_isOverlayUpdatePending = false;
		g_isUpdatePending = false;
		g_isUpdateReady = false;
		g_qpcLastUpdate = Time::queryPerformanceCounter() - Time::msToQpc(DELAYED_FLIP_MODE_TIMEOUT_MS);
		g_qpcUpdateStart = g_qpcLastUpdate;
		g_presentEndVsyncCount = D3dDdi::KernelModeThunks::getVsyncCounter();
		g_flipEndVsyncCount = g_presentEndVsyncCount;
	}

	void presentationBlt(CompatRef<IDirectDrawSurface7> dst, CompatRef<IDirectDrawSurface7> src)
	{
		LOG_FUNC("RealPrimarySurface::presentationBlt", dst, src);
		D3dDdi::ScopedCriticalSection lock;
		auto srcResource = D3dDdi::Device::findResource(
			DDraw::DirectDrawSurface::getDriverResourceHandle(src.get()));
		auto dstResource = D3dDdi::Device::findResource(
			DDraw::DirectDrawSurface::getDriverResourceHandle(dst.get()));
		if (!srcResource || !dstResource)
		{
			return;
		}

		D3DDDIARG_BLT blt = {};
		blt.hSrcResource = *srcResource;
		blt.SrcSubResourceIndex = DDraw::DirectDrawSurface::getSubResourceIndex(src.get());
		blt.SrcRect = srcResource->getRect(blt.SrcSubResourceIndex);
		blt.hDstResource = *dstResource;
		blt.DstSubResourceIndex = DDraw::DirectDrawSurface::getSubResourceIndex(dst.get());
		blt.DstRect = dstResource->getRect(blt.DstSubResourceIndex);
		dstResource->presentationBlt(blt, srcResource);
	}

	void present(CompatWeakPtr<IDirectDrawSurface7> src, bool isOverlayOnly)
	{
		LOG_FUNC("RealPrimarySurface::present", src, isOverlayOnly);

		Gdi::VirtualScreen::update();

		Gdi::GuiThread::execute([&]()
			{
				auto statsWindow = Gdi::GuiThread::getStatsWindow();
				if (statsWindow)
				{
					if (!isOverlayOnly)
					{
						statsWindow->m_presentCount++;
						statsWindow->m_present.add();
					}
					statsWindow->update();
				}

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

		if (src ? !g_isFullscreen : !g_presentationWindow)
		{
			Gdi::Window::present(nullptr);
			return;
		}

		const bool useFlip = src && g_isFullscreen;
		Win32::DisplayMode::MonitorInfo mi = {};
		CompatWeakPtr<IDirectDrawSurface7> frontBuffer;
		CompatPtr<IDirectDrawSurface7> backBuffer;
		CompatPtr<IDirectDrawSurface7> windowedSrc;

		if (src)
		{
			mi = DDraw::PrimarySurface::getMonitorInfo();
			frontBuffer = g_frontBuffer;
			if (g_isFullscreen)
			{
				backBuffer = getBackBuffer();
			}
		}
		else
		{
			mi = Win32::DisplayMode::getMonitorInfo(MonitorFromWindow(g_presentationWindow, MONITOR_DEFAULTTOPRIMARY));
			if (!DDraw::TagSurface::findFullscreenWindow())
			{
				frontBuffer = D3dDdi::SurfaceRepository::getPrimaryRepo().getWindowedPrimary();
			}
		}

		if (g_presentationWindow && !backBuffer)
		{
			D3dDdi::SurfaceRepository* repo = nullptr;
			if (src)
			{
				repo = DDraw::DirectDrawSurface::getSurfaceRepository(*frontBuffer);
			}
			else
			{
				repo = &D3dDdi::SurfaceRepository::getPrimaryRepo();
				windowedSrc = repo->getWindowedSrc(mi.rcEmulated);
				if (!windowedSrc)
				{
					return;
				}
				src = windowedSrc;
			}

			backBuffer = repo->getWindowedBackBuffer(
				mi.rcDpiAware.right - mi.rcDpiAware.left, mi.rcDpiAware.bottom - mi.rcDpiAware.top);
			if (!backBuffer)
			{
				return;
			}
		}

		Gdi::Region excludeRegion(mi.rcEmulated);
		Gdi::Window::present(excludeRegion);
		presentationBlt(*backBuffer, *src);
		if (useFlip)
		{
			if (g_isExclusiveFullscreen)
			{
				frontBuffer->Flip(frontBuffer, backBuffer, DDFLIP_WAIT);
			}
			else
			{
				*g_deviceWindowPtr = g_presentationWindow;
				frontBuffer->Flip(frontBuffer, nullptr, DDFLIP_WAIT);
				*g_deviceWindowPtr = g_deviceWindow;
			}
		}
		else if (frontBuffer)
		{
			if (!g_clipper)
			{
				CALL_ORIG_PROC(DirectDrawCreateClipper)(0, &g_clipper.getRef(), nullptr);
			}
			g_clipper->SetHWnd(g_clipper, 0, g_presentationWindow);
			frontBuffer->SetClipper(frontBuffer, g_clipper);
			frontBuffer->Blt(frontBuffer, nullptr, backBuffer, nullptr, DDBLT_WAIT, nullptr);
		}
		else
		{
			HDC dstDc = GetWindowDC(g_presentationWindow);
			HDC srcDc = nullptr;
			D3dDdi::Resource::setReadOnlyLock(true);
			backBuffer->GetDC(backBuffer, &srcDc);
			D3dDdi::Resource::setReadOnlyLock(false);
			CALL_ORIG_FUNC(BitBlt)(dstDc, 0, 0, mi.rcDpiAware.right - mi.rcDpiAware.left, mi.rcDpiAware.bottom - mi.rcDpiAware.top,
				srcDc, 0, 0, SRCCOPY);
			backBuffer->ReleaseDC(backBuffer, srcDc);
			ReleaseDC(g_presentationWindow, dstDc);
		}
	}

	void setFullscreenPresentationMode(const Win32::DisplayMode::MonitorInfo& mi)
	{
		Gdi::Cursor::setEmulated(!IsRectEmpty(&mi.rcEmulated) && !Overlay::Steam::isOverlayOpen());
		Gdi::Cursor::setMonitorClipRect(mi.rcEmulated);
	}

	void updateNow(CompatWeakPtr<IDirectDrawSurface7> src, bool isOverlayOnly)
	{
		present(src, isOverlayOnly);

		{
			Compat::ScopedCriticalSection lock(g_presentCs);
			g_isOverlayUpdatePending = false;
			g_isUpdatePending = false;
			g_isUpdateReady = false;
		}

		g_presentEndVsyncCount = D3dDdi::KernelModeThunks::getVsyncCounter() + 1;
	}

	void updatePresentationParams()
	{
		LOG_FUNC("RealPrimarySurface::updatePresentationParams");

		HWND fullscreenWindow = nullptr;
		if (isProcessActive())
		{
			if (g_isFullscreen && IsWindowVisible(g_deviceWindow) && !IsIconic(g_deviceWindow))
			{
				fullscreenWindow = g_deviceWindow;
			}
			else
			{
				fullscreenWindow = Gdi::Window::getFullscreenWindow();
			}
		}
		else if (g_isFullscreen)
		{
			setFullscreenPresentationMode({});
			return;
		}

		HWND fullscreenPresentationWindow = nullptr;
		if (fullscreenWindow)
		{
			Gdi::Window::setDpiAwareness(fullscreenWindow, true);
			fullscreenPresentationWindow = Gdi::Window::getPresentationWindow(fullscreenWindow);
		}

		g_presentationWindow = fullscreenPresentationWindow;

		if (g_presentationWindow)
		{
			auto& mi = Win32::DisplayMode::getMonitorInfo(MonitorFromWindow(g_presentationWindow, MONITOR_DEFAULTTOPRIMARY));
			auto& mr = mi.rcDpiAware;

			Gdi::GuiThread::execute([&]()
				{
					Win32::ScopedDpiAwareness dpiAwareness;
					CALL_ORIG_FUNC(SetWindowPos)(g_presentationWindow, HWND_TOPMOST, mr.left, mr.top, 0, 0,
						SWP_NOACTIVATE | SWP_NOSENDCHANGING | SWP_NOREDRAW | SWP_NOOWNERZORDER | SWP_SHOWWINDOW | SWP_NOSIZE);
					CALL_ORIG_FUNC(SetWindowPos)(g_presentationWindow, nullptr, 0, 0, mr.right - mr.left, mr.bottom - mr.top,
						SWP_NOACTIVATE | SWP_NOSENDCHANGING | SWP_NOREDRAW | SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_NOMOVE);
				});

			setFullscreenPresentationMode(mi);
		}
		else
		{
			setFullscreenPresentationMode({});
		}

		static HWND prevFullscreenWindow = nullptr;
		if (prevFullscreenWindow && prevFullscreenWindow != fullscreenWindow)
		{
			Gdi::Window::setDpiAwareness(prevFullscreenWindow, false);
			HWND prevFullscreenPresentationWindow = Gdi::Window::getPresentationWindow(prevFullscreenWindow);
			if (prevFullscreenPresentationWindow)
			{
				Gdi::Window::updatePresentationWindowPos(prevFullscreenPresentationWindow, prevFullscreenWindow);
			}
		}
		prevFullscreenWindow = fullscreenWindow;
	}

	unsigned WINAPI updateThreadProc(LPVOID /*lpParameter*/)
	{
		int msUntilUpdateReady = 0;
		while (true)
		{
			Win32::Thread::rotateCpuAffinity();
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
	HRESULT RealPrimarySurface::create(CompatRef<IDirectDraw> dd)
	{
		LOG_FUNC("RealPrimarySurface::create", &dd);

		DDSURFACEDESC desc = {};
		desc.dwSize = sizeof(desc);
		desc.dwFlags = DDSD_CAPS | DDSD_BACKBUFFERCOUNT;
		desc.ddsCaps.dwCaps = DDSCAPS_PRIMARYSURFACE | DDSCAPS_3DDEVICE | DDSCAPS_COMPLEX | DDSCAPS_FLIP;
		desc.dwBackBufferCount = g_isExclusiveFullscreen ? 2 : 1;

		CompatPtr<IDirectDrawSurface> surface;
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
			LOG_ONCE("ERROR: Failed to create the real primary surface: " << Compat::hex(result));
			return result;
		}

		auto ddLcl = DDraw::DirectDraw::getInt(dd.get()).lpLcl;
		auto tagSurface = DDraw::TagSurface::get(ddLcl);
		if (!tagSurface)
		{
			LOG_ONCE("ERROR: TagSurface not found");
			return DDERR_GENERIC;
		}

		g_isFullscreen = 0 != desc.dwBackBufferCount;
		auto& mi = PrimarySurface::getMonitorInfo();
		g_monitorRect = g_isFullscreen && g_isExclusiveFullscreen ? mi.rcReal : mi.rcDpiAware;

		g_tagSurface = tagSurface;
		g_frontBuffer = CompatPtr<IDirectDrawSurface7>::from(surface.get()).detach();
		g_frontBuffer->SetPrivateData(g_frontBuffer, IID_IReleaseNotifier,
			&g_releaseNotifier, sizeof(&g_releaseNotifier), DDSPD_IUNKNOWNPOINTER);
		
		g_deviceWindowPtr = (0 != desc.dwBackBufferCount) ? DDraw::DirectDraw::getDeviceWindowPtr(dd.get()) : nullptr;
		g_deviceWindow = g_deviceWindowPtr ? *g_deviceWindowPtr : nullptr;

		onRestore();
		return DD_OK;
	}

	HRESULT RealPrimarySurface::flip(CompatPtr<IDirectDrawSurface7> surfaceTargetOverride, DWORD flags)
	{
		const DWORD flipInterval = getFlipInterval(flags);
		if (0 == flipInterval ||
			Time::qpcToMs(Time::queryPerformanceCounter() - g_qpcLastUpdate) < DELAYED_FLIP_MODE_TIMEOUT_MS)
		{
			PrimarySurface::waitForIdle();
			Compat::ScopedCriticalSection lock(g_presentCs);
			g_isDelayedFlipPending = true;
			g_isOverlayUpdatePending = false;
			g_isUpdatePending = false;
			g_isUpdateReady = false;
			g_lastUpdateThreadId = GetCurrentThreadId();
		}
		else
		{
			D3dDdi::KernelModeThunks::waitForVsyncCounter(g_presentEndVsyncCount);
			g_isUpdateReady = true;
			flush();
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

		static UINT lastOverlayCheckVsyncCount = 0;
		if (vsyncCount != lastOverlayCheckVsyncCount)
		{
			updatePresentationParams();
			setPresentationWindowTopmost();
			Gdi::Cursor::update();
			Gdi::Caret::blink();
			auto statsWindow = Gdi::GuiThread::getStatsWindow();
			if (statsWindow && statsWindow->isVisible())
			{
				statsWindow->updateStats();
				g_qpcLastUpdate = Time::queryPerformanceCounter();
			}
			Overlay::Steam::flush();
			lastOverlayCheckVsyncCount = vsyncCount;
		}

		bool isOverlayOnly = false;

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
				else if (g_isOverlayUpdatePending)
				{
					g_qpcLastUpdate = Time::queryPerformanceCounter();
					g_isUpdateReady = true;
					isOverlayOnly = true;
				}
			}

			if (!g_isUpdateReady)
			{
				return -1;
			}
		}

		auto primary(DDraw::PrimarySurface::getPrimary());
		CompatWeakPtr<IDirectDrawSurface7> src;
		if (primary && SUCCEEDED(primary->IsLost(primary)) &&
			g_frontBuffer && SUCCEEDED(g_frontBuffer->IsLost(g_frontBuffer)))
		{
			src = g_isDelayedFlipPending ? g_lastFlipSurface->getDDS() : primary;
		}

		updateNow(src, isOverlayOnly);

		RECT emptyRect = {};
		HRESULT result = src ? src->BltFast(src, 0, 0, src, &emptyRect, DDBLTFAST_WAIT) : DD_OK;
		if (DDERR_SURFACEBUSY == result || DDERR_LOCKEDSURFACES == result)
		{
			scheduleUpdate();
		}

		return 0;
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

	HWND RealPrimarySurface::getPresentationWindow()
	{
		return g_presentationWindow;
	}

	CompatWeakPtr<IDirectDrawSurface7> RealPrimarySurface::getSurface()
	{
		return g_frontBuffer;
	}

	HWND RealPrimarySurface::getTopmost()
	{
		return g_presentationWindow ? g_presentationWindow : HWND_TOPMOST;
	}

	void RealPrimarySurface::init()
	{
		g_isExclusiveFullscreen = Config::Settings::FullscreenMode::EXCLUSIVE == Config::fullscreenMode.get() ||
			!IsWindows8OrGreater();
		Dll::createThread(&updateThreadProc, nullptr, THREAD_PRIORITY_TIME_CRITICAL);
	}

	bool RealPrimarySurface::isExclusiveFullscreen()
	{
		return g_isExclusiveFullscreen;
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
		LOG_FUNC("RealPrimarySurface::restore");
		DDraw::ScopedThreadLock lock;
		auto dd(g_tagSurface->getDD());
		if (g_isFullscreen && FAILED(dd->TestCooperativeLevel(dd)))
		{
			return DDERR_NOEXCLUSIVEMODE;
		}

		HRESULT result = g_frontBuffer->Restore(g_frontBuffer);
		if (SUCCEEDED(result))
		{
			release();
			return create(*CompatPtr<IDirectDraw>::from(dd.get()));
		}
		return LOG_RESULT(result);
	}

	void RealPrimarySurface::scheduleOverlayUpdate()
	{
		Compat::ScopedCriticalSection lock(g_presentCs);
		g_isOverlayUpdatePending = true;
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

	void RealPrimarySurface::setPresentationWindowTopmost()
	{
		if (g_presentationWindow && IsWindowVisible(g_presentationWindow))
		{
			Gdi::GuiThread::execute([&]()
				{
					CALL_ORIG_FUNC(SetWindowPos)(g_presentationWindow, HWND_TOPMOST, 0, 0, 0, 0,
					SWP_NOSIZE | SWP_NOMOVE | SWP_NOACTIVATE | SWP_NOSENDCHANGING | SWP_NOREDRAW | SWP_NOOWNERZORDER);
				});
		}
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

	void RealPrimarySurface::waitForFlipFpsLimit()
	{
		static long long g_qpcPrevWaitEnd = Time::queryPerformanceCounter() - Time::g_qpcFrequency;
		auto qpcNow = Time::queryPerformanceCounter();
		auto qpcWaitEnd = g_qpcPrevWaitEnd + Time::g_qpcFrequency / Config::fpsLimiter.getParam();
		if (qpcNow - qpcWaitEnd >= 0)
		{
			g_qpcPrevWaitEnd = qpcNow;
			g_qpcDelayedFlipEnd = qpcNow;
			return;
		}
		g_qpcPrevWaitEnd = qpcWaitEnd;
		g_qpcDelayedFlipEnd = qpcWaitEnd;

		{
			DDraw::ScopedThreadUnlock unlock;
			Compat::ScopedThreadPriority prio(THREAD_PRIORITY_TIME_CRITICAL);
			while (Time::qpcToMs(qpcWaitEnd - qpcNow) > 0)
			{
				Time::waitForNextTick();
				flush();
				qpcNow = Time::queryPerformanceCounter();
			}

			while (qpcWaitEnd - qpcNow > 0)
			{
				qpcNow = Time::queryPerformanceCounter();
			}
		}
		g_qpcDelayedFlipEnd = Time::queryPerformanceCounter();
	}
}
