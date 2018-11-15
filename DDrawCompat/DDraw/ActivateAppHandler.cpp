#include <atomic>

#include <ddraw.h>

#include "Common/Hook.h"
#include "Common/Log.h"
#include "DDraw/ActivateAppHandler.h"
#include "DDraw/RealPrimarySurface.h"
#include "Gdi/Gdi.h"
#include "Win32/DisplayMode.h"
#include "Win32/FontSmoothing.h"

namespace
{
	Win32::FontSmoothing::SystemSettings g_fontSmoothingSettings = {};
	WNDPROC g_origDdWndProc = nullptr;
	std::atomic<DWORD> g_activateAppThreadId = 0;
	HWND g_delayedFocusWnd = nullptr;

	void activateApp()
	{
		Win32::FontSmoothing::setSystemSettings(g_fontSmoothingSettings);
	}

	void deactivateApp()
	{
		g_fontSmoothingSettings = Win32::FontSmoothing::getSystemSettings();
		Win32::FontSmoothing::setSystemSettings(Win32::FontSmoothing::g_origSystemSettings);
	}

	LRESULT CALLBACK ddWndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
	{
		LOG_FUNC("ddWndProc", hwnd, Compat::hex(uMsg), Compat::hex(wParam), Compat::hex(lParam));
		static bool isDisplayChangeNotificationEnabled = true;

		switch (uMsg)
		{
		case WM_ACTIVATEAPP:
		{
			DDraw::RealPrimarySurface::disableUpdates();
			isDisplayChangeNotificationEnabled = false;
			if (TRUE == wParam)
			{
				activateApp();
			}
			else
			{
				deactivateApp();
			}

			g_activateAppThreadId = GetCurrentThreadId();
			g_delayedFocusWnd = nullptr;
			LRESULT result = g_origDdWndProc(hwnd, uMsg, wParam, lParam);
			g_activateAppThreadId = 0;
			if (g_delayedFocusWnd)
			{
				CALL_ORIG_FUNC(SetFocus)(g_delayedFocusWnd);
			}

			isDisplayChangeNotificationEnabled = true;
			DDraw::RealPrimarySurface::enableUpdates();
			return LOG_RESULT(result);
		}

		case WM_DISPLAYCHANGE:
		{
			// Fix for alt-tabbing in Commandos 2
			if (!isDisplayChangeNotificationEnabled)
			{
				return LOG_RESULT(0);
			}
			break;
		}
		}

		return LOG_RESULT(g_origDdWndProc(hwnd, uMsg, wParam, lParam));
	}

	HWND WINAPI setFocus(HWND hWnd)
	{
		if (GetCurrentThreadId() == g_activateAppThreadId && IsWindow(hWnd))
		{
			g_delayedFocusWnd = hWnd;
			return GetFocus();
		}
		return CALL_ORIG_FUNC(SetFocus)(hWnd);
	}

	BOOL WINAPI showWindow(HWND hWnd, int  nCmdShow)
	{
		if (GetCurrentThreadId() == g_activateAppThreadId && IsWindow(hWnd))
		{
			BOOL result = IsWindowVisible(hWnd);
			ShowWindowAsync(hWnd, nCmdShow);
			return result;
		}
		return CALL_ORIG_FUNC(ShowWindow)(hWnd, nCmdShow);
	}
}

namespace DDraw
{
	namespace ActivateAppHandler
	{
		void installHooks()
		{
			HOOK_FUNCTION(user32, SetFocus, setFocus);
			HOOK_FUNCTION(user32, ShowWindow, showWindow);
		}

		void setCooperativeLevel(HWND hwnd, DWORD flags)
		{
			static bool isDdWndProcHooked = false;
			if ((flags & DDSCL_FULLSCREEN) && !isDdWndProcHooked)
			{
				g_origDdWndProc = reinterpret_cast<WNDPROC>(GetWindowLongPtr(hwnd, GWLP_WNDPROC));
				Compat::hookFunction(reinterpret_cast<void*&>(g_origDdWndProc), ddWndProc);
				isDdWndProcHooked = true;
			}
		}
	}
}
