#include "Common/CompatPtr.h"
#include "Common/CompatRef.h"
#include "Common/Log.h"
#include "DDraw/ActivateAppHandler.h"
#include "DDraw/DirectDraw.h"
#include "DDraw/DisplayMode.h"
#include "DDraw/Surfaces/FullScreenTagSurface.h"
#include "DDraw/Surfaces/PrimarySurface.h"
#include "DDraw/Surfaces/SurfaceImpl.h"
#include "Gdi/Gdi.h"
#include "Win32/FontSmoothing.h"

extern HWND g_mainWindow;

namespace
{
	bool g_isActive = true;
	HWND g_fullScreenCooperativeWindow = nullptr;
	DWORD g_fullScreenCooperativeFlags = 0;
	Win32::FontSmoothing::SystemSettings g_fontSmoothingSettings = {};
	HHOOK g_callWndProcHook = nullptr;

	void handleActivateApp(bool isActivated);

	void activateApp(CompatRef<IDirectDraw7> dd)
	{
		if (!(g_fullScreenCooperativeFlags & DDSCL_NOWINDOWCHANGES))
		{
			ShowWindow(g_fullScreenCooperativeWindow, SW_RESTORE);
			HWND lastActivePopup = GetLastActivePopup(g_fullScreenCooperativeWindow);
			if (lastActivePopup && lastActivePopup != g_fullScreenCooperativeWindow)
			{
				BringWindowToTop(lastActivePopup);
			}
		}

		dd->SetCooperativeLevel(&dd, g_fullScreenCooperativeWindow, g_fullScreenCooperativeFlags);
		auto dm = DDraw::DisplayMode::getDisplayMode(dd);
		dd->SetDisplayMode(&dd, dm.dwWidth, dm.dwHeight, 32, dm.dwRefreshRate, 0);

		auto primary(DDraw::PrimarySurface::getPrimary());
		if (primary && SUCCEEDED(primary->Restore(primary)))
		{
			DDraw::SurfaceImpl<IDirectDrawSurface7>::fixSurfacePtrs(*primary);
			Gdi::invalidate(nullptr);
		}

		Win32::FontSmoothing::setSystemSettings(g_fontSmoothingSettings);
	}

	void deactivateApp(CompatRef<IDirectDraw7> dd)
	{
		dd->RestoreDisplayMode(&dd);
		dd->SetCooperativeLevel(&dd, g_fullScreenCooperativeWindow, DDSCL_NORMAL);

		if (!(g_fullScreenCooperativeFlags & DDSCL_NOWINDOWCHANGES))
		{
			ShowWindow(g_fullScreenCooperativeWindow, SW_SHOWMINNOACTIVE);
		}

		g_fontSmoothingSettings = Win32::FontSmoothing::getSystemSettings();
		Win32::FontSmoothing::setSystemSettings(Win32::FontSmoothing::g_origSystemSettings);
	}

	LRESULT CALLBACK callWndProc(int nCode, WPARAM wParam, LPARAM lParam)
	{
		auto ret = reinterpret_cast<CWPSTRUCT*>(lParam);
		Compat::LogEnter("callWndProc", nCode, wParam, ret);

		if (HC_ACTION == nCode && WM_ACTIVATEAPP == ret->message)
		{
			const bool isActivated = TRUE == ret->wParam;
			handleActivateApp(isActivated);
		}

		LRESULT result = CallNextHookEx(nullptr, nCode, wParam, lParam);
		Compat::LogLeave("callWndProc", nCode, wParam, ret) << result;
		return result;
	}

	void handleActivateApp(bool isActivated)
	{
		Compat::LogEnter("handleActivateApp", isActivated);

		if (isActivated == g_isActive)
		{
			return;
		}
		g_isActive = isActivated;

		if (!isActivated)
		{
			Gdi::disableEmulation();
		}

		auto dd(DDraw::FullScreenTagSurface::getFullScreenDirectDraw());
		if (dd)
		{
			if (isActivated)
			{
				activateApp(*dd);
			}
			else
			{
				deactivateApp(*dd);
			}
		}

		if (isActivated)
		{
			Gdi::enableEmulation();
		}

		Compat::LogLeave("handleActivateApp", isActivated);
	}
}

namespace DDraw
{
	namespace ActivateAppHandler
	{
		void installHooks()
		{
			const DWORD threadId = GetCurrentThreadId();
			g_callWndProcHook = SetWindowsHookEx(WH_CALLWNDPROC, callWndProc, nullptr, threadId);
		}

		bool isActive()
		{
			return g_isActive;
		}

		void setFullScreenCooperativeLevel(HWND hwnd, DWORD flags)
		{
			g_fullScreenCooperativeWindow = hwnd;
			g_fullScreenCooperativeFlags = flags;
		}

		void uninstallHooks()
		{
			UnhookWindowsHookEx(g_callWndProcHook);
		}
	}
}
