#include "CompatActivateAppHandler.h"
#include "CompatDirectDraw.h"
#include "CompatDirectDrawSurface.h"
#include "CompatDisplayMode.h"
#include "CompatGdi.h"
#include "CompatPrimarySurface.h"
#include "CompatPtr.h"
#include "CompatRef.h"
#include "DDrawLog.h"

extern HWND g_mainWindow;

namespace
{
	bool g_isActive = true;
	CompatWeakPtr<IUnknown> g_fullScreenDirectDraw = nullptr;
	HWND g_fullScreenCooperativeWindow = nullptr;
	DWORD g_fullScreenCooperativeFlags = 0;
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
		auto dm = CompatDisplayMode::getDisplayMode(dd);
		dd->SetDisplayMode(&dd, dm.width, dm.height, 32, dm.refreshRate, dm.flags);

		auto primary(CompatPrimarySurface::getPrimary());
		if (primary && SUCCEEDED(primary->Restore(primary)))
		{
			CompatDirectDrawSurface<IDirectDrawSurface7>::fixSurfacePtrs(*primary);
			CompatGdi::invalidate(nullptr);
		}
	}

	void deactivateApp(CompatRef<IDirectDraw7> dd)
	{
		dd->RestoreDisplayMode(&dd);
		dd->SetCooperativeLevel(&dd, g_fullScreenCooperativeWindow, DDSCL_NORMAL);

		if (!(g_fullScreenCooperativeFlags & DDSCL_NOWINDOWCHANGES))
		{
			ShowWindow(g_fullScreenCooperativeWindow, SW_SHOWMINNOACTIVE);
		}
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
			CompatGdi::disableEmulation();
		}

		if (g_fullScreenDirectDraw)
		{
			CompatPtr<IDirectDraw7> dd(Compat::queryInterface<IDirectDraw7>(g_fullScreenDirectDraw.get()));
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
			CompatGdi::enableEmulation();
		}

		Compat::LogLeave("handleActivateApp", isActivated);
	}
}

namespace CompatActivateAppHandler
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

	void setFullScreenCooperativeLevel(CompatWeakPtr<IUnknown> dd, HWND hwnd, DWORD flags)
	{
		g_fullScreenDirectDraw = dd;
		g_fullScreenCooperativeWindow = hwnd;
		g_fullScreenCooperativeFlags = flags;
	}

	void uninstallHooks()
	{
		UnhookWindowsHookEx(g_callWndProcHook);
	}
}
