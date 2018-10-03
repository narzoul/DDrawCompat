#include <memory>
#include <vector>

#include "Common/CompatPtr.h"
#include "Common/Hook.h"
#include "Common/Time.h"
#include "Config/Config.h"
#include "D3dDdi/Device.h"
#include "D3dDdi/KernelModeThunks.h"
#include "DDraw/DirectDraw.h"
#include "DDraw/DirectDrawSurface.h"
#include "DDraw/IReleaseNotifier.h"
#include "DDraw/RealPrimarySurface.h"
#include "DDraw/ScopedThreadLock.h"
#include "DDraw/Surfaces/PrimarySurface.h"
#include "DDraw/Types.h"
#include "Gdi/AccessGuard.h"
#include "Gdi/Gdi.h"
#include "Gdi/VirtualScreen.h"
#include "Gdi/Window.h"
#include "Win32/DisplayMode.h"

namespace
{
	struct BltToWindowViaGdiArgs
	{
		std::unique_ptr<HDC__, void(*)(HDC)> virtualScreenDc;
		Gdi::Region* primaryRegion;

		BltToWindowViaGdiArgs()
			: virtualScreenDc(nullptr, &Gdi::VirtualScreen::deleteDc)
			, primaryRegion(nullptr)
		{
		}
	};

	void onRelease();
	DWORD WINAPI updateThreadProc(LPVOID lpParameter);

	CompatWeakPtr<IDirectDrawSurface7> g_frontBuffer;
	CompatWeakPtr<IDirectDrawSurface7> g_paletteConverter;
	CompatWeakPtr<IDirectDrawClipper> g_clipper;
	DDSURFACEDESC2 g_surfaceDesc = {};
	DDraw::IReleaseNotifier g_releaseNotifier(onRelease);

	bool g_stopUpdateThread = false;
	HANDLE g_updateThread = nullptr;
	unsigned int g_disableUpdateCount = 0;
	bool g_isFlipPending = false;
	bool g_isPresentPending = false;
	bool g_isUpdatePending = false;
	bool g_isFullScreen = false;
	bool g_waitingForPrimaryUnlock = false;
	UINT g_flipIntervalDriverOverride = UINT_MAX;
	UINT g_lastFlipFrameCount = 0;
	DDraw::Surface* g_lastFlipSurface = nullptr;
	long long g_qpcLastUpdate = 0;

	CompatPtr<IDirectDrawSurface7> getBackBuffer();
	CompatPtr<IDirectDrawSurface7> getLastSurface();

	BOOL CALLBACK addVisibleLayeredWindowToVector(HWND hwnd, LPARAM lParam)
	{
		if (IsWindowVisible(hwnd) && (GetWindowLongPtr(hwnd, GWL_EXSTYLE) & WS_EX_LAYERED) &&
			!Gdi::Window::isPresentationWindow(hwnd))
		{
			auto& visibleLayeredWindows = *reinterpret_cast<std::vector<HWND>*>(lParam);
			visibleLayeredWindows.push_back(hwnd);
		}
		return TRUE;
	}

	BOOL CALLBACK bltToWindow(HWND hwnd, LPARAM lParam)
	{
		if (!IsWindowVisible(hwnd) || !Gdi::Window::isPresentationWindow(hwnd))
		{
			return TRUE;
		}

		g_clipper->SetHWnd(g_clipper, 0, hwnd);
		auto src = reinterpret_cast<IDirectDrawSurface7*>(lParam);
		g_frontBuffer->Blt(g_frontBuffer, nullptr, src, nullptr, DDBLT_WAIT, nullptr);
		return TRUE;
	}

	BOOL CALLBACK bltToWindowViaGdi(HWND hwnd, LPARAM lParam)
	{
		if (!IsWindowVisible(hwnd) || !Gdi::Window::isPresentationWindow(hwnd))
		{
			return TRUE;
		}

		auto window = Gdi::Window::get(GetParent(hwnd));
		if (!window)
		{
			return TRUE;
		}

		Gdi::Region visibleRegion = window->getVisibleRegion();
		if (visibleRegion.isEmpty())
		{
			return TRUE;
		}

		auto& args = *reinterpret_cast<BltToWindowViaGdiArgs*>(lParam);
		if (args.primaryRegion)
		{
			visibleRegion -= *args.primaryRegion;
			if (visibleRegion.isEmpty())
			{
				return TRUE;
			}
		}

		if (!args.virtualScreenDc)
		{
			args.virtualScreenDc.reset(Gdi::VirtualScreen::createDc());
			if (!args.virtualScreenDc)
			{
				return FALSE;
			}
		}

		Gdi::GdiAccessGuard accessGuard(Gdi::ACCESS_READ);
		HDC presentationWindowDc = GetWindowDC(hwnd);
		RECT rect = window->getWindowRect();
		CALL_ORIG_FUNC(BitBlt)(presentationWindowDc, 0, 0, rect.right - rect.left, rect.bottom - rect.top,
			args.virtualScreenDc.get(), rect.left, rect.top, SRCCOPY);
		ReleaseDC(hwnd, presentationWindowDc);

		return TRUE;
	}

	void bltVisibleLayeredWindowsToBackBuffer()
	{
		std::vector<HWND> visibleLayeredWindows;
		EnumThreadWindows(Gdi::getGdiThreadId(), addVisibleLayeredWindowToVector,
			reinterpret_cast<LPARAM>(&visibleLayeredWindows));

		if (visibleLayeredWindows.empty())
		{
			return;
		}

		auto backBuffer(getBackBuffer());
		if (!backBuffer)
		{
			return;
		}

		HDC backBufferDc = nullptr;
		backBuffer->GetDC(backBuffer, &backBufferDc);

		for (auto it = visibleLayeredWindows.rbegin(); it != visibleLayeredWindows.rend(); ++it)
		{
			HDC windowDc = GetWindowDC(*it);
			HRGN rgn = Gdi::getVisibleWindowRgn(*it);
			RECT wr = {};
			GetWindowRect(*it, &wr);
			
			SelectClipRgn(backBufferDc, rgn);
			CALL_ORIG_FUNC(BitBlt)(backBufferDc, wr.left, wr.top, wr.right - wr.left, wr.bottom - wr.top,
				windowDc, 0, 0, SRCCOPY);
			SelectClipRgn(backBufferDc, nullptr);

			DeleteObject(rgn);
			ReleaseDC(*it, windowDc);
		}

		backBuffer->ReleaseDC(backBuffer, backBufferDc);
	}

	void bltToPrimaryChain(CompatRef<IDirectDrawSurface7> src)
	{
		if (!g_isFullScreen)
		{
			EnumThreadWindows(Gdi::getGdiThreadId(), bltToWindow, reinterpret_cast<LPARAM>(&src));
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

	UINT getFlipIntervalFromFlags(DWORD flags)
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

	UINT getFlipInterval(DWORD flags)
	{
		if (0 == g_flipIntervalDriverOverride)
		{
			return 0;
		}

		UINT flipInterval = getFlipIntervalFromFlags(flags);
		if (UINT_MAX != g_flipIntervalDriverOverride)
		{
			return max(flipInterval, g_flipIntervalDriverOverride);
		}
		return flipInterval;
	}

	bool isFlipPending()
	{
		if (g_isFlipPending)
		{
			g_isFlipPending = static_cast<int>(D3dDdi::KernelModeThunks::getLastDisplayedFrameCount() -
				g_lastFlipFrameCount) < 0;
		}
		return g_isFlipPending;
	}

	bool isPresentPending()
	{
		if (g_isPresentPending)
		{
			g_isPresentPending = static_cast<int>(D3dDdi::KernelModeThunks::getLastDisplayedFrameCount() -
				D3dDdi::KernelModeThunks::getLastSubmittedFrameCount()) < 0;
		}
		return g_isPresentPending;
	}

	void onRelease()
	{
		Compat::LogEnter("RealPrimarySurface::onRelease");

		g_frontBuffer = nullptr;
		g_clipper.release();
		g_isFullScreen = false;
		g_isFlipPending = false;
		g_isPresentPending = false;
		g_waitingForPrimaryUnlock = false;
		g_paletteConverter.release();
		g_surfaceDesc = {};

		Compat::LogLeave("RealPrimarySurface::onRelease");
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
			CALL_ORIG_PROC(DirectDrawCreateClipper, 0, &g_clipper.getRef(), nullptr);
			g_frontBuffer->SetClipper(g_frontBuffer, g_clipper);
		}

		g_surfaceDesc = desc;
		g_isFullScreen = isFlippable;
		g_flipIntervalDriverOverride = UINT_MAX;
		g_isFlipPending = false;
		g_isPresentPending = false;
		g_isUpdatePending = false;
		g_qpcLastUpdate = Time::queryPerformanceCounter() - Time::msToQpc(Config::delayedFlipModeTimeout);

		if (isFlippable)
		{
			D3dDdi::KernelModeThunks::setFlipIntervalOverride(UINT_MAX);
			g_frontBuffer->Flip(g_frontBuffer, nullptr, DDFLIP_WAIT | DDFLIP_NOVSYNC);
			g_flipIntervalDriverOverride = D3dDdi::KernelModeThunks::getLastFlipInterval();
			g_frontBuffer->Flip(g_frontBuffer, nullptr, DDFLIP_WAIT);
			if (0 == g_flipIntervalDriverOverride)
			{
				g_flipIntervalDriverOverride = D3dDdi::KernelModeThunks::getLastFlipInterval();
				if (1 == g_flipIntervalDriverOverride)
				{
					g_flipIntervalDriverOverride = UINT_MAX;
				}
			}
			g_frontBuffer->Flip(g_frontBuffer, nullptr, DDFLIP_WAIT);
			D3dDdi::KernelModeThunks::setFlipIntervalOverride(0);
			g_lastFlipFrameCount = D3dDdi::KernelModeThunks::getLastSubmittedFrameCount();
		}

		D3dDdi::KernelModeThunks::waitForVerticalBlank();
	}

	void presentToPrimaryChain(CompatWeakPtr<IDirectDrawSurface7> src)
	{
		Compat::LogEnter("RealPrimarySurface::presentToPrimaryChain", src.get());

		Gdi::VirtualScreen::update();

		BltToWindowViaGdiArgs bltToWindowViaGdiArgs;
		if (!g_frontBuffer || !src || DDraw::RealPrimarySurface::isLost())
		{
			EnumThreadWindows(Gdi::getGdiThreadId(), bltToWindowViaGdi,
				reinterpret_cast<LPARAM>(&bltToWindowViaGdiArgs));
			Compat::LogLeave("RealPrimarySurface::presentToPrimaryChain", src.get()) << false;
			return;
		}

		Gdi::Region primaryRegion(D3dDdi::KernelModeThunks::getMonitorRect());
		bltToWindowViaGdiArgs.primaryRegion = &primaryRegion;
		EnumThreadWindows(Gdi::getGdiThreadId(), bltToWindowViaGdi,
			reinterpret_cast<LPARAM>(&bltToWindowViaGdiArgs));

		Gdi::DDrawAccessGuard accessGuard(Gdi::ACCESS_READ, DDraw::PrimarySurface::isGdiSurface(src.get()));
		if (DDraw::PrimarySurface::getDesc().ddpfPixelFormat.dwRGBBitCount <= 8)
		{
			HDC paletteConverterDc = nullptr;
			g_paletteConverter->GetDC(g_paletteConverter, &paletteConverterDc);
			HDC srcDc = nullptr;
			D3dDdi::Device::setReadOnlyGdiLock(true);
			src->GetDC(src, &srcDc);
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
			bltVisibleLayeredWindowsToBackBuffer();
		}

		Compat::LogLeave("RealPrimarySurface::presentToPrimaryChain", src.get());
	}

	void updateNow(CompatWeakPtr<IDirectDrawSurface7> src, UINT flipInterval)
	{
		DDraw::ScopedThreadLock lock;
		if (flipInterval <= 1 && isPresentPending())
		{
			g_isUpdatePending = true;
			return;
		}

		presentToPrimaryChain(src);
		g_isUpdatePending = false;

		if (!g_isFullScreen)
		{
			g_isPresentPending = true;
			return;
		}

		if (flipInterval > 1 && isPresentPending())
		{
			--flipInterval;
		}

		D3dDdi::KernelModeThunks::setFlipIntervalOverride(flipInterval);
		g_frontBuffer->Flip(g_frontBuffer, nullptr, DDFLIP_WAIT);

		// Workaround for Windows 8 multimon display glitches when presenting from GDI shared primary surface
		D3dDdi::KernelModeThunks::setFlipIntervalOverride(UINT_MAX);
		g_frontBuffer->Flip(g_frontBuffer, getLastSurface(), DDFLIP_WAIT);
		D3dDdi::KernelModeThunks::setFlipIntervalOverride(0);

		g_isPresentPending = 0 != flipInterval;
	}

	void updateNowIfNotBusy()
	{
		auto primary(DDraw::PrimarySurface::getPrimary());
		RECT emptyRect = {};
		HRESULT result = primary ? primary->BltFast(primary, 0, 0, primary, &emptyRect, DDBLTFAST_WAIT) : DD_OK;
		g_waitingForPrimaryUnlock = DDERR_SURFACEBUSY == result || DDERR_LOCKEDSURFACES == result;

		if (!g_waitingForPrimaryUnlock && DDERR_SURFACELOST != result)
		{
			const auto msSinceLastUpdate = Time::qpcToMs(Time::queryPerformanceCounter() - g_qpcLastUpdate);
			updateNow(primary, msSinceLastUpdate > Config::delayedFlipModeTimeout ? 0 : 1);
		}
	}

	DWORD WINAPI updateThreadProc(LPVOID /*lpParameter*/)
	{
		const int msPresentDelayAfterVBlank = 1;
		bool waitForVBlank = true;

		while (!g_stopUpdateThread)
		{
			if (waitForVBlank)
			{
				D3dDdi::KernelModeThunks::waitForVerticalBlank();
				if (!g_isFullScreen)
				{
					g_isPresentPending = false;
				}
			}

			Sleep(msPresentDelayAfterVBlank);

			DDraw::ScopedThreadLock lock;
			waitForVBlank = Time::qpcToMs(Time::queryPerformanceCounter() -
				D3dDdi::KernelModeThunks::getQpcLastVerticalBlank()) >= msPresentDelayAfterVBlank;

			if (waitForVBlank && g_isUpdatePending && 0 == g_disableUpdateCount && !isPresentPending())
			{
				updateNowIfNotBusy();
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
			Compat::Log() << "Failed to create the palette converter surface: " << Compat::hex(result);
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
			Compat::Log() << "Failed to create the real primary surface: " << Compat::hex(result);
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

	void RealPrimarySurface::disableUpdates()
	{
		DDraw::ScopedThreadLock lock;
		--g_disableUpdateCount;
	}

	void RealPrimarySurface::enableUpdates()
	{
		DDraw::ScopedThreadLock lock;
		++g_disableUpdateCount;
	}

	HRESULT RealPrimarySurface::flip(CompatPtr<IDirectDrawSurface7> surfaceTargetOverride, DWORD flags)
	{
		DDraw::ScopedThreadLock lock;

		auto primary(PrimarySurface::getPrimary());
		const bool isFlipEmulated = 0 != (PrimarySurface::getOrigCaps() & DDSCAPS_SYSTEMMEMORY);
		if (isFlipEmulated && !surfaceTargetOverride)
		{
			surfaceTargetOverride = PrimarySurface::getBackBuffer();
		}

		HRESULT result = primary->Flip(primary, surfaceTargetOverride, DDFLIP_WAIT);
		if (FAILED(result))
		{
			return result;
		}

		DWORD flipInterval = getFlipInterval(flags);
		const auto msSinceLastUpdate = Time::qpcToMs(Time::queryPerformanceCounter() - g_qpcLastUpdate);
		const bool isFlipDelayed = msSinceLastUpdate >= 0 && msSinceLastUpdate <= Config::delayedFlipModeTimeout;
		if (isFlipDelayed)
		{
			CompatPtr<IDirectDrawSurface7> prevPrimarySurface(
				surfaceTargetOverride ? surfaceTargetOverride : PrimarySurface::getLastSurface());
			updateNow(prevPrimarySurface, flipInterval);
			g_isUpdatePending = true;
		}

		if (isFlipEmulated)
		{
			surfaceTargetOverride->Blt(surfaceTargetOverride, nullptr, primary, nullptr, DDBLT_WAIT, nullptr);
		}

		if (!isFlipDelayed)
		{
			updateNow(primary, flipInterval);
		}

		if (0 != flipInterval)
		{
			g_isFlipPending = true;
			g_lastFlipFrameCount = D3dDdi::KernelModeThunks::getLastSubmittedFrameCount();
		}

		g_lastFlipSurface = nullptr;
		if (g_isFlipPending && !isFlipEmulated)
		{
			g_lastFlipSurface = Surface::getSurface(
				surfaceTargetOverride ? *surfaceTargetOverride : *PrimarySurface::getLastSurface());
		}

		return DD_OK;
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

	void RealPrimarySurface::setPalette()
	{
		DDraw::ScopedThreadLock lock;
		if (g_surfaceDesc.ddpfPixelFormat.dwRGBBitCount <= 8)
		{
			g_frontBuffer->SetPalette(g_frontBuffer, PrimarySurface::s_palette);
		}

		updatePalette();
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

	void RealPrimarySurface::updatePalette()
	{
		DDraw::ScopedThreadLock lock;
		if (PrimarySurface::s_palette)
		{
			update();
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

		while (isFlipPending())
		{
			D3dDdi::KernelModeThunks::waitForVerticalBlank();
		}

		return true;
	}
}
