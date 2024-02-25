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
#include <Win32/DisplayMode.h>
#include <Win32/DpiAwareness.h>
#include <Win32/Thread.h>

namespace
{
	const unsigned DELAYED_FLIP_MODE_TIMEOUT_MS = 200;

	void onRelease();
	void updatePresentationWindow();

	CompatWeakPtr<IDirectDrawSurface7> g_defaultPrimary;

	CompatWeakPtr<IDirectDrawSurface7> g_frontBuffer;
	CompatWeakPtr<IDirectDrawSurface7> g_windowedBackBuffer;
	CompatWeakPtr<IDirectDrawClipper> g_clipper;
	RECT g_monitorRect = {};
	DDSURFACEDESC2 g_surfaceDesc = {};
	DDraw::IReleaseNotifier g_releaseNotifier(onRelease);

	bool g_emulatedCursor = false;
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
	long long g_qpcUpdatePresentationWindow = 0;

	CompatPtr<IDirectDrawSurface7> getBackBuffer();
	CompatPtr<IDirectDrawSurface7> getLastSurface();

	void bltToPrimaryChain(CompatRef<IDirectDrawSurface7> src)
	{
		if (!g_isFullscreen)
		{
			updatePresentationWindow();

			if (g_presentationWindow)
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

			Gdi::Window::present(*g_frontBuffer, g_presentationWindow ? *g_windowedBackBuffer : src, *g_clipper);
			return;
		}

		auto backBuffer(getBackBuffer());
		if (backBuffer)
		{
			backBuffer->Blt(backBuffer, nullptr, &src, nullptr, DDBLT_WAIT, nullptr);
		}
	}

	BOOL WINAPI createDefaultPrimaryEnum(
		GUID* lpGUID, LPSTR /*lpDriverDescription*/, LPSTR lpDriverName, LPVOID lpContext, HMONITOR /*hm*/)
	{
		auto& deviceName = *static_cast<std::wstring*>(lpContext);
		if (deviceName != std::wstring(lpDriverName, lpDriverName + strlen(lpDriverName)))
		{
			return TRUE;
		}

		auto tagSurface = DDraw::TagSurface::findFullscreenWindow();
		LOG_DEBUG << "Creating " << (tagSurface ? "fullscreen" : "windowed") << " default primary";

		DDraw::SuppressResourceFormatLogs suppressResourceFormatLogs;
		if (tagSurface)
		{
			DDSURFACEDESC desc = {};
			desc.dwSize = sizeof(desc);
			desc.dwFlags = DDSD_CAPS;
			desc.ddsCaps.dwCaps = DDSCAPS_PRIMARYSURFACE;

			CompatPtr<IDirectDraw> dd(tagSurface->getDD());
			CompatPtr<IDirectDrawSurface> primary;
			dd.get()->lpVtbl->CreateSurface(dd, &desc, &primary.getRef(), nullptr);
			g_defaultPrimary = CompatPtr<IDirectDrawSurface7>(primary).detach();
		}
		else
		{
			CompatPtr<IDirectDraw7> dd;
			if (FAILED(CALL_ORIG_PROC(DirectDrawCreateEx)(
				lpGUID, reinterpret_cast<void**>(&dd.getRef()), IID_IDirectDraw7, nullptr)))
			{
				return FALSE;
			}
			DDraw::DirectDraw::onCreate(lpGUID, *dd);

			if (FAILED(dd.get()->lpVtbl->SetCooperativeLevel(dd, nullptr, DDSCL_NORMAL)))
			{
				return FALSE;
			}

			DDSURFACEDESC2 desc = {};
			desc.dwSize = sizeof(desc);
			desc.dwFlags = DDSD_CAPS;
			desc.ddsCaps.dwCaps = DDSCAPS_PRIMARYSURFACE;
			dd.get()->lpVtbl->CreateSurface(dd, &desc, &g_defaultPrimary.getRef(), nullptr);
		}

		return nullptr != g_defaultPrimary;
	}

	void createDefaultPrimary()
	{
		if (!Dll::g_isHooked ||
			(g_defaultPrimary ? SUCCEEDED(g_defaultPrimary->IsLost(g_defaultPrimary)) : g_frontBuffer))
		{
			return;
		}

		DDraw::RealPrimarySurface::destroyDefaultPrimary();

		auto dm = Win32::DisplayMode::getEmulatedDisplayMode();
		if (dm.deviceName.empty())
		{
			return;
		}

		CALL_ORIG_PROC(DirectDrawEnumerateExA)(createDefaultPrimaryEnum, &dm.deviceName, DDENUM_ATTACHEDSECONDARYDEVICES);
	}

	CompatPtr<IDirectDrawSurface7> createWindowedBackBuffer(DDraw::TagSurface& tagSurface, DWORD width, DWORD height)
	{
		auto resource = DDraw::DirectDrawSurface::getDriverResourceHandle(*tagSurface.getDDS());
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

		auto& repo = device->getRepo();
		D3dDdi::SurfaceRepository::Surface surface = {};
		repo.getSurface(surface, width, height, D3DDDIFMT_X8R8G8B8,
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

		if (g_windowedBackBuffer)
		{
			auto resource = D3dDdi::Device::findResource(
				DDraw::DirectDrawSurface::getDriverResourceHandle(*g_windowedBackBuffer));
			resource->setFullscreenMode(false);
		}

		DDraw::RealPrimarySurface::schedulePresentationWindowUpdate();

		g_defaultPrimary = nullptr;
		g_frontBuffer = nullptr;
		g_lastFlipSurface = nullptr;
		g_windowedBackBuffer.release();
		g_clipper.release();
		g_isFullscreen = false;
		g_surfaceDesc = {};
		g_tagSurface = nullptr;

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
		g_surfaceDesc = desc;

		if (g_isExclusiveFullscreen && 0 != (desc.ddsCaps.dwCaps & DDSCAPS_FLIP))
		{
			g_frontBuffer->Flip(g_frontBuffer, getLastSurface(), DDFLIP_WAIT);
			D3dDdi::KernelModeThunks::waitForVsyncCounter(D3dDdi::KernelModeThunks::getVsyncCounter() + 1);
		}

		if (g_windowedBackBuffer)
		{
			g_windowedBackBuffer->Restore(g_windowedBackBuffer);
		}

		auto gdiResource = DDraw::PrimarySurface::getGdiResource();
		if (gdiResource)
		{
			D3dDdi::Device::setGdiResourceHandle(gdiResource);
		}

		updatePresentationWindow();

		Compat::ScopedCriticalSection lock(g_presentCs);
		g_isOverlayUpdatePending = false;
		g_isUpdatePending = false;
		g_isUpdateReady = false;
		g_qpcLastUpdate = Time::queryPerformanceCounter() - Time::msToQpc(DELAYED_FLIP_MODE_TIMEOUT_MS);
		g_qpcUpdateStart = g_qpcLastUpdate;
		g_presentEndVsyncCount = D3dDdi::KernelModeThunks::getVsyncCounter();
		g_flipEndVsyncCount = g_presentEndVsyncCount;
	}

	void presentToPrimaryChain(CompatWeakPtr<IDirectDrawSurface7> src, bool isOverlayOnly)
	{
		LOG_FUNC("RealPrimarySurface::presentToPrimaryChain", src, isOverlayOnly);

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

		if (!g_frontBuffer || !src || DDraw::RealPrimarySurface::isLost())
		{
			Gdi::Window::present(nullptr);
			return;
		}

		Gdi::Region excludeRegion(DDraw::PrimarySurface::getMonitorRect());
		Gdi::Window::present(excludeRegion);
		bltToPrimaryChain(*src);
	}

	void updateNow(CompatWeakPtr<IDirectDrawSurface7> src, bool isOverlayOnly)
	{
		{
			Compat::ScopedCriticalSection lock(g_presentCs);
			g_isOverlayUpdatePending = false;
			g_isUpdatePending = false;
			g_isUpdateReady = false;
		}

		presentToPrimaryChain(src, isOverlayOnly);

		if (g_isFullscreen)
		{
			updatePresentationWindow();
			*g_deviceWindowPtr = g_presentationWindow;
			g_frontBuffer->Flip(g_frontBuffer, g_isExclusiveFullscreen ? getBackBuffer() : nullptr, DDFLIP_WAIT);
			*g_deviceWindowPtr = g_deviceWindow;
		}
		g_presentEndVsyncCount = D3dDdi::KernelModeThunks::getVsyncCounter() + 1;
	}

	void updatePresentationWindow()
	{
		LOG_FUNC("RealPrimarySurface::updatePresentationWindow");

		HWND fullscreenWindow = nullptr;
		if (isProcessActive())
		{
			if (g_isFullscreen && IsWindowVisible(g_deviceWindow) && !IsIconic(g_deviceWindow))
			{
				if (g_isExclusiveFullscreen)
				{
					return;
				}
				fullscreenWindow = g_deviceWindow;
			}
			else if (g_frontBuffer && DDraw::PrimarySurface::getPrimary() && SUCCEEDED(g_frontBuffer->IsLost(g_frontBuffer)))
			{
				fullscreenWindow = Gdi::Window::getFullscreenWindow();
			}
		}
		else if (g_isFullscreen)
		{
			return;
		}

		HWND fullscreenPresentationWindow = nullptr;
		if (fullscreenWindow)
		{
			Gdi::Window::setDpiAwareness(fullscreenWindow, true);
			fullscreenPresentationWindow = Gdi::Window::getPresentationWindow(fullscreenWindow);
		}

		if (g_windowedBackBuffer)
		{
			auto resource = D3dDdi::Device::findResource(
				DDraw::DirectDrawSurface::getDriverResourceHandle(*g_windowedBackBuffer));
			resource->setFullscreenMode(fullscreenPresentationWindow);
		}

		g_presentationWindow = fullscreenPresentationWindow;

		if (g_presentationWindow)
		{
			Gdi::GuiThread::execute([&]()
				{
					Win32::ScopedDpiAwareness dpiAwareness;
					CALL_ORIG_FUNC(SetWindowPos)(g_presentationWindow, HWND_TOPMOST, g_monitorRect.left, g_monitorRect.top, 0, 0,
						SWP_NOACTIVATE | SWP_NOSENDCHANGING | SWP_NOREDRAW | SWP_NOOWNERZORDER | SWP_SHOWWINDOW | SWP_NOSIZE);
					CALL_ORIG_FUNC(SetWindowPos)(g_presentationWindow, nullptr, 0, 0,
						g_monitorRect.right - g_monitorRect.left, g_monitorRect.bottom - g_monitorRect.top,
						SWP_NOACTIVATE | SWP_NOSENDCHANGING | SWP_NOREDRAW | SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_NOMOVE);
				});
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

		if (Gdi::Cursor::isEmulated() != g_emulatedCursor)
		{
			Gdi::Cursor::setEmulated(g_emulatedCursor);
		}
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
		DDraw::ScopedThreadLock lock;
		const auto& mi = Win32::DisplayMode::getMonitorInfo(
			D3dDdi::KernelModeThunks::getAdapterInfo(*CompatPtr<IDirectDraw7>::from(&dd)).deviceName);
		auto prevMonitorRect = g_monitorRect;
		g_monitorRect = g_isExclusiveFullscreen ? mi.rcReal : mi.rcDpiAware;

		DDSURFACEDESC desc = {};
		desc.dwSize = sizeof(desc);
		desc.dwFlags = DDSD_CAPS | DDSD_BACKBUFFERCOUNT;
		desc.ddsCaps.dwCaps = DDSCAPS_PRIMARYSURFACE | DDSCAPS_3DDEVICE | DDSCAPS_COMPLEX | DDSCAPS_FLIP;
		desc.dwBackBufferCount = g_isExclusiveFullscreen ? 2 : 1;

		auto prevIsFullscreen = g_isFullscreen;
		g_isFullscreen = true;
		CompatPtr<IDirectDrawSurface> surface;
		HRESULT result = dd->CreateSurface(&dd, &desc, &surface.getRef(), nullptr);

		if (DDERR_NOEXCLUSIVEMODE == result)
		{
			g_isFullscreen = false;
			g_monitorRect = mi.rcDpiAware;
			desc.dwFlags = DDSD_CAPS;
			desc.ddsCaps.dwCaps = DDSCAPS_PRIMARYSURFACE;
			desc.dwBackBufferCount = 0;
			result = dd->CreateSurface(&dd, &desc, &surface.getRef(), nullptr);
		}

		if (FAILED(result))
		{
			LOG_INFO << "ERROR: Failed to create the real primary surface: " << Compat::hex(result);
			g_monitorRect = prevMonitorRect;
			g_isFullscreen = prevIsFullscreen;
			return result;
		}

		auto ddLcl = DDraw::DirectDraw::getInt(dd.get()).lpLcl;
		auto tagSurface = DDraw::TagSurface::get(ddLcl);
		if (!tagSurface)
		{
			LOG_INFO << "ERROR: TagSurface not found";
			g_monitorRect = prevMonitorRect;
			g_isFullscreen = prevIsFullscreen;
			return DDERR_GENERIC;
		}

		if (0 == desc.dwBackBufferCount)
		{
			g_windowedBackBuffer = createWindowedBackBuffer(*tagSurface,
				g_monitorRect.right - g_monitorRect.left, g_monitorRect.bottom - g_monitorRect.top).detach();
			if (!g_windowedBackBuffer)
			{
				g_monitorRect = prevMonitorRect;
				g_isFullscreen = prevIsFullscreen;
				return DDERR_GENERIC;
			}
		}

		g_tagSurface = tagSurface;
		g_frontBuffer = CompatPtr<IDirectDrawSurface7>::from(surface.get()).detach();
		g_frontBuffer->SetPrivateData(g_frontBuffer, IID_IReleaseNotifier,
			&g_releaseNotifier, sizeof(&g_releaseNotifier), DDSPD_IUNKNOWNPOINTER);
		
		g_deviceWindowPtr = (0 != desc.dwBackBufferCount) ? DDraw::DirectDraw::getDeviceWindowPtr(dd.get()) : nullptr;
		g_deviceWindow = g_deviceWindowPtr ? *g_deviceWindowPtr : nullptr;

		onRestore();
		return DD_OK;
	}

	void RealPrimarySurface::destroyDefaultPrimary()
	{
		if (g_defaultPrimary)
		{
			LOG_DEBUG << "Destroying default primary";
			g_defaultPrimary.release();
		}
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
			setPresentationWindowTopmost();
			Gdi::Cursor::update();
			Gdi::Caret::blink();
			auto statsWindow = Gdi::GuiThread::getStatsWindow();
			if (statsWindow && statsWindow->isVisible())
			{
				statsWindow->updateStats();
				g_qpcLastUpdate = Time::queryPerformanceCounter();
			}
			lastOverlayCheckVsyncCount = vsyncCount;
		}

		bool isPresentationWindowUpdateNeeded = false;

		{
			Compat::ScopedCriticalSection lock(g_presentCs);
			isPresentationWindowUpdateNeeded =
				0 != g_qpcUpdatePresentationWindow && Time::queryPerformanceCounter() - g_qpcUpdatePresentationWindow >= 0 ||
				!isProcessActive();
		}

		if (isPresentationWindowUpdateNeeded)
		{
			g_qpcUpdatePresentationWindow = 0;
			updatePresentationWindow();
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

		createDefaultPrimary();
		if (!g_defaultPrimary && g_frontBuffer && FAILED(g_frontBuffer->IsLost(g_frontBuffer)))
		{
			restore();
		}

		auto primary(DDraw::PrimarySurface::getPrimary());
		CompatWeakPtr<IDirectDrawSurface7> src;
		if (primary && SUCCEEDED(primary->IsLost(primary)))
		{
			src = g_isDelayedFlipPending ? g_lastFlipSurface->getDDS() : primary;
		}
		else
		{
			src = DDraw::PrimarySurface::getGdiPrimary();
		}

		RECT emptyRect = {};
		HRESULT result = src ? src->BltFast(src, 0, 0, src, &emptyRect, DDBLTFAST_WAIT) : DD_OK;
		if (DDERR_SURFACEBUSY == result || DDERR_LOCKEDSURFACES == result)
		{
			return 1;
		}

		updateNow(src, isOverlayOnly);
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

	RECT RealPrimarySurface::getMonitorRect()
	{
		return g_monitorRect;
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
		if (g_defaultPrimary)
		{
			destroyDefaultPrimary();
			createDefaultPrimary();
			return DD_OK;
		}

		auto dd(g_tagSurface->getDD());
		if (g_isFullscreen && FAILED(dd->TestCooperativeLevel(dd)))
		{
			return DDERR_NOEXCLUSIVEMODE;
		}

		release();
		return create(*CompatPtr<IDirectDraw>::from(dd.get()));
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

	void RealPrimarySurface::schedulePresentationWindowUpdate()
	{
		Compat::ScopedCriticalSection lock(g_presentCs);
		g_qpcUpdatePresentationWindow = Time::queryPerformanceCounter() + Time::g_qpcFrequency / 5;
	}

	void RealPrimarySurface::setEmulatedCursor(bool emulated)
	{
		g_emulatedCursor = emulated;
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
		g_qpcDelayedFlipEnd = Time::queryPerformanceCounter();
	}
}
