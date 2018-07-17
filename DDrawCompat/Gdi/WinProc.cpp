#define WIN32_LEAN_AND_MEAN

#include <map>
#include <memory>
#include <set>

#include <dwmapi.h>
#include <Windows.h>

#include "Common/Log.h"
#include "Common/ScopedCriticalSection.h"
#include "Gdi/AccessGuard.h"
#include "Gdi/Dc.h"
#include "Gdi/ScrollBar.h"
#include "Gdi/ScrollFunctions.h"
#include "Gdi/TitleBar.h"
#include "Gdi/Window.h"
#include "Gdi/WinProc.h"

namespace
{
	HHOOK g_callWndRetProcHook = nullptr;
	HWINEVENTHOOK g_objectStateChangeEventHook = nullptr;
	std::set<Gdi::WindowPosChangeNotifyFunc> g_windowPosChangeNotifyFuncs;

	void disableDwmAttributes(HWND hwnd);
	void onActivate(HWND hwnd);
	void onCreateWindow(HWND hwnd);
	void onDestroyWindow(HWND hwnd);
	void onMenuSelect();
	void onWindowPosChanged(HWND hwnd);
	void removeDropShadow(HWND hwnd);

	LRESULT CALLBACK callWndRetProc(int nCode, WPARAM wParam, LPARAM lParam)
	{
		auto ret = reinterpret_cast<CWPRETSTRUCT*>(lParam);
		Compat::LogEnter("callWndRetProc", nCode, wParam, ret);

		if (HC_ACTION == nCode)
		{
			if (WM_CREATE == ret->message)
			{
				onCreateWindow(ret->hwnd);
			}
			else if (WM_DESTROY == ret->message)
			{
				onDestroyWindow(ret->hwnd);
			}
			else if (WM_WINDOWPOSCHANGED == ret->message)
			{
				onWindowPosChanged(ret->hwnd);
			}
			else if (WM_ACTIVATE == ret->message)
			{
				onActivate(ret->hwnd);
			}
			else if (WM_COMMAND == ret->message)
			{
				auto notifCode = HIWORD(ret->wParam);
				if (ret->lParam && (EN_HSCROLL == notifCode || EN_VSCROLL == notifCode))
				{
					Gdi::ScrollFunctions::updateScrolledWindow(reinterpret_cast<HWND>(ret->lParam));
				}
			}
			else if (WM_MENUSELECT == ret->message)
			{
				onMenuSelect();
			}
		}

		LRESULT result = CallNextHookEx(nullptr, nCode, wParam, lParam);
		Compat::LogLeave("callWndRetProc", nCode, wParam, ret) << result;
		return result;
	}

	void disableDwmAttributes(HWND hwnd)
	{
		DWMNCRENDERINGPOLICY ncRenderingPolicy = DWMNCRP_DISABLED;
		DwmSetWindowAttribute(hwnd, DWMWA_NCRENDERING_POLICY,
			&ncRenderingPolicy, sizeof(ncRenderingPolicy));

		BOOL disableTransitions = TRUE;
		DwmSetWindowAttribute(hwnd, DWMWA_TRANSITIONS_FORCEDISABLED,
			&disableTransitions, sizeof(disableTransitions));
	}

	BOOL CALLBACK initTopLevelWindow(HWND hwnd, LPARAM /*lParam*/)
	{
		onCreateWindow(hwnd);
		if (!(GetWindowLongPtr(hwnd, GWL_EXSTYLE) & WS_EX_LAYERED))
		{
			RedrawWindow(hwnd, nullptr, nullptr,
				RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN | RDW_UPDATENOW);
		}
		return TRUE;
	}

	bool isTopLevelNonLayeredWindow(HWND hwnd)
	{
		return !(GetWindowLongPtr(hwnd, GWL_EXSTYLE) & WS_EX_LAYERED) &&
			(!(GetWindowLongPtr(hwnd, GWL_STYLE) & WS_CHILD) || GetParent(hwnd) == GetDesktopWindow());
	}

	void CALLBACK objectStateChangeEvent(
		HWINEVENTHOOK /*hWinEventHook*/,
		DWORD /*event*/,
		HWND hwnd,
		LONG idObject,
		LONG /*idChild*/,
		DWORD /*dwEventThread*/,
		DWORD /*dwmsEventTime*/)
	{
		if (OBJID_TITLEBAR == idObject || OBJID_HSCROLL == idObject || OBJID_VSCROLL == idObject)
		{
			if (!hwnd)
			{
				return;
			}

			HDC windowDc = GetWindowDC(hwnd);
			HDC compatDc = Gdi::Dc::getDc(windowDc);
			if (compatDc)
			{
				Gdi::GdiAccessGuard accessGuard(Gdi::ACCESS_WRITE);
				if (OBJID_TITLEBAR == idObject)
				{
					Gdi::TitleBar(hwnd, compatDc).drawButtons();
				}
				else if (OBJID_HSCROLL == idObject)
				{
					Gdi::ScrollBar(hwnd, compatDc).drawHorizArrows();
				}
				else if (OBJID_VSCROLL == idObject)
				{
					Gdi::ScrollBar(hwnd, compatDc).drawVertArrows();
				}
				Gdi::Dc::releaseDc(windowDc);
			}
			ReleaseDC(hwnd, windowDc);
		}
	}

	void onActivate(HWND hwnd)
	{
		RECT windowRect = {};
		GetWindowRect(hwnd, &windowRect);
		RECT clientRect = {};
		GetClientRect(hwnd, &clientRect);
		POINT clientOrigin = {};
		ClientToScreen(hwnd, &clientOrigin);
		OffsetRect(&windowRect, -clientOrigin.x, -clientOrigin.y);

		HRGN ncRgn = CreateRectRgnIndirect(&windowRect);
		HRGN clientRgn = CreateRectRgnIndirect(&clientRect);
		CombineRgn(ncRgn, ncRgn, clientRgn, RGN_DIFF);
		RedrawWindow(hwnd, nullptr, ncRgn, RDW_FRAME | RDW_INVALIDATE);
		DeleteObject(clientRgn);
		DeleteObject(ncRgn);
	}

	void onCreateWindow(HWND hwnd)
	{
		if (isTopLevelNonLayeredWindow(hwnd))
		{
			disableDwmAttributes(hwnd);
			removeDropShadow(hwnd);
			Gdi::Window::add(hwnd);
		}
	}

	void onDestroyWindow(HWND hwnd)
	{
		Gdi::Window::remove(hwnd);
	}

	void onMenuSelect()
	{
		HWND menuWindow = FindWindow(reinterpret_cast<LPCSTR>(0x8000), nullptr);
		while (menuWindow)
		{
			RedrawWindow(menuWindow, nullptr, nullptr, RDW_INVALIDATE);
			menuWindow = FindWindowEx(nullptr, menuWindow, reinterpret_cast<LPCSTR>(0x8000), nullptr);
		}
	}

	void onWindowPosChanged(HWND hwnd)
	{
		const ATOM menuAtom = 0x8000;
		if (menuAtom == GetClassLongPtr(hwnd, GCW_ATOM))
		{
			SetWindowLongPtr(hwnd, GWL_EXSTYLE, GetWindowLongPtr(hwnd, GWL_EXSTYLE) & ~WS_EX_LAYERED);
		}

		for (auto notifyFunc : g_windowPosChangeNotifyFuncs)
		{
			notifyFunc();
		}

		if (isTopLevelNonLayeredWindow(hwnd))
		{
			Gdi::Window::updateAll();
		}
	}

	void removeDropShadow(HWND hwnd)
	{
		const auto style = GetClassLongPtr(hwnd, GCL_STYLE);
		if (style & CS_DROPSHADOW)
		{
			SetClassLongPtr(hwnd, GCL_STYLE, style ^ CS_DROPSHADOW);
		}
	}
}

namespace Gdi
{
	namespace WinProc
	{
		void installHooks()
		{
			const DWORD threadId = Gdi::getGdiThreadId();
			g_callWndRetProcHook = SetWindowsHookEx(WH_CALLWNDPROCRET, callWndRetProc, nullptr, threadId);
			g_objectStateChangeEventHook = SetWinEventHook(EVENT_OBJECT_STATECHANGE, EVENT_OBJECT_STATECHANGE,
				nullptr, &objectStateChangeEvent, 0, threadId, WINEVENT_OUTOFCONTEXT);

			EnumThreadWindows(threadId, initTopLevelWindow, 0);
		}

		void watchWindowPosChanges(WindowPosChangeNotifyFunc notifyFunc)
		{
			g_windowPosChangeNotifyFuncs.insert(notifyFunc);
		}

		void uninstallHooks()
		{
			UnhookWinEvent(g_objectStateChangeEventHook);
			UnhookWindowsHookEx(g_callWndRetProcHook);
		}
	}
}
