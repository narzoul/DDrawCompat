#include <map>
#include <set>

#include <Windows.h>

#include <Common/Hook.h>
#include <Common/Log.h>
#include <Common/ScopedSrwLock.h>
#include <Dll/Dll.h>
#include <DDraw/RealPrimarySurface.h>
#include <DDraw/Surfaces/PrimarySurface.h>
#include <Gdi/CompatDc.h>
#include <Gdi/Cursor.h>
#include <Gdi/Dc.h>
#include <Gdi/PresentationWindow.h>
#include <Gdi/ScrollBar.h>
#include <Gdi/ScrollFunctions.h>
#include <Gdi/TitleBar.h>
#include <Gdi/Window.h>
#include <Gdi/WinProc.h>
#include <Overlay/ConfigWindow.h>
#include <Win32/DisplayMode.h>

namespace
{
	struct WindowProc
	{
		WNDPROC wndProcA;
		WNDPROC wndProcW;
	};

	std::map<HMENU, UINT> g_menuMaxHeight;
	std::set<Gdi::WindowPosChangeNotifyFunc> g_windowPosChangeNotifyFuncs;

	Compat::SrwLock g_windowProcSrwLock;
	std::map<HWND, WindowProc> g_windowProc;

	WindowProc getWindowProc(HWND hwnd);
	bool isTopLevelWindow(HWND hwnd);
	bool isUser32ScrollBar(HWND hwnd);
	void onCreateWindow(HWND hwnd);
	void onDestroyWindow(HWND hwnd);
	void onGetMinMaxInfo(MINMAXINFO& mmi);
	void onInitMenuPopup(HMENU menu);
	void onUninitMenuPopup(HMENU menu);
	void onWindowPosChanged(HWND hwnd, const WINDOWPOS& wp);
	void onWindowPosChanging(HWND hwnd, WINDOWPOS& wp);
	void setWindowProc(HWND hwnd, WNDPROC wndProcA, WNDPROC wndProcW);

	LRESULT CALLBACK ddcWindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam,
		decltype(&CallWindowProcA) callWindowProc, WNDPROC wndProc)
	{
		LOG_FUNC("ddcWindowProc", Compat::WindowMessageStruct(hwnd, uMsg, wParam, lParam));

		switch (uMsg)
		{
		case WM_ACTIVATEAPP:
			if (!wParam)
			{
				auto configWindow = Gdi::PresentationWindow::getConfigWindow();
				if (configWindow)
				{
					configWindow->setVisible(false);
				}
			}
			break;

		case WM_DISPLAYCHANGE:
		{
			if (0 != wParam)
			{
				return 0;
			}
			wParam = Win32::DisplayMode::getBpp();
			break;
		}

		case WM_GETMINMAXINFO:
			onGetMinMaxInfo(*reinterpret_cast<MINMAXINFO*>(lParam));
			break;

		case WM_SYNCPAINT:
			if (isTopLevelWindow(hwnd))
			{
				Gdi::Window::onSyncPaint(hwnd);
				return 0;
			}
			break;

		case WM_UNINITMENUPOPUP:
			onUninitMenuPopup(reinterpret_cast<HMENU>(wParam));
			break;

		case WM_WINDOWPOSCHANGED:
			onWindowPosChanged(hwnd, *reinterpret_cast<WINDOWPOS*>(lParam));
			break;
		}

		LRESULT result = callWindowProc(wndProc, hwnd, uMsg, wParam, lParam);

		switch (uMsg)
		{
		case WM_CTLCOLORSCROLLBAR:
			if (reinterpret_cast<HWND>(lParam) != hwnd &&
				isUser32ScrollBar(reinterpret_cast<HWND>(lParam)))
			{
				Gdi::ScrollBar::onCtlColorScrollBar(hwnd, wParam, lParam, result);
			}
			break;

		case WM_INITMENUPOPUP:
			onInitMenuPopup(reinterpret_cast<HMENU>(wParam));
			break;

		case WM_NCDESTROY:
			onDestroyWindow(hwnd);
			break;

		case WM_STYLECHANGED:
			if (isTopLevelWindow(hwnd))
			{
				Gdi::Window::onStyleChanged(hwnd, wParam);
			}
			break;

		case WM_WINDOWPOSCHANGING:
			onWindowPosChanging(hwnd, *reinterpret_cast<WINDOWPOS*>(lParam));
			break;
		}

		return LOG_RESULT(result);
	}

	LRESULT CALLBACK ddcWindowProcA(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
	{
		return ddcWindowProc(hwnd, uMsg, wParam, lParam, CallWindowProcA, getWindowProc(hwnd).wndProcA);
	}

	LRESULT CALLBACK ddcWindowProcW(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
	{
		return ddcWindowProc(hwnd, uMsg, wParam, lParam, CallWindowProcW, getWindowProc(hwnd).wndProcW);
	}

	LONG getWindowLong(HWND hWnd, int nIndex,
		decltype(&GetWindowLongA) origGetWindowLong, WNDPROC(WindowProc::* wndProc))
	{
		if (GWL_WNDPROC == nIndex)
		{
			Compat::ScopedSrwLockExclusive lock(g_windowProcSrwLock);
			auto it = g_windowProc.find(hWnd);
			if (it != g_windowProc.end())
			{
				return reinterpret_cast<LONG>(it->second.*wndProc);
			}
		}
		return origGetWindowLong(hWnd, nIndex);
	}

	LONG WINAPI getWindowLongA(HWND hWnd, int nIndex)
	{
		LOG_FUNC("GetWindowLongA", hWnd, nIndex);
		return LOG_RESULT(getWindowLong(hWnd, nIndex, CALL_ORIG_FUNC(GetWindowLongA), &WindowProc::wndProcA));
	}

	LONG WINAPI getWindowLongW(HWND hWnd, int nIndex)
	{
		LOG_FUNC("GetWindowLongW", hWnd, nIndex);
		return LOG_RESULT(getWindowLong(hWnd, nIndex, CALL_ORIG_FUNC(GetWindowLongW), &WindowProc::wndProcW));
	}

	WindowProc getWindowProc(HWND hwnd)
	{
		Compat::ScopedSrwLockExclusive lock(g_windowProcSrwLock);
		return g_windowProc[hwnd];
	}

	bool isTopLevelWindow(HWND hwnd)
	{
		return GetDesktopWindow() == GetAncestor(hwnd, GA_PARENT);
	}

	bool isUser32ScrollBar(HWND hwnd)
	{
		WNDCLASS wc = {};
		static const ATOM sbAtom = static_cast<ATOM>(GetClassInfo(nullptr, "ScrollBar", &wc));
		if (sbAtom != GetClassLong(hwnd, GCW_ATOM))
		{
			return false;
		}

		auto it = g_windowProc.find(hwnd);
		if (it == g_windowProc.end())
		{
			return false;
		}

		return GetModuleHandle("comctl32") != Compat::getModuleHandleFromAddress(
			IsWindowUnicode(hwnd) ? it->second.wndProcW : it->second.wndProcA);
	}

	void onCreateWindow(HWND hwnd)
	{
		LOG_FUNC("onCreateWindow", hwnd);

		{
			Compat::ScopedSrwLockExclusive lock(g_windowProcSrwLock);
			if (g_windowProc.find(hwnd) != g_windowProc.end())
			{
				return;
			}

			auto wndProcA = reinterpret_cast<WNDPROC>(CALL_ORIG_FUNC(GetWindowLongA)(hwnd, GWL_WNDPROC));
			auto wndProcW = reinterpret_cast<WNDPROC>(CALL_ORIG_FUNC(GetWindowLongW)(hwnd, GWL_WNDPROC));
			g_windowProc[hwnd] = { wndProcA, wndProcW };
			setWindowProc(hwnd, ddcWindowProcA, ddcWindowProcW);
		}

		if (!isTopLevelWindow(hwnd))
		{
			return;
		}

		char className[64] = {};
		GetClassName(hwnd, className, sizeof(className));
		if (std::string(className) == "CompatWindowDesktopReplacement")
		{
			// Disable VirtualizeDesktopPainting shim
			SendNotifyMessage(hwnd, WM_CLOSE, 0, 0);
			return;
		}

		Gdi::Window::updateAll();
	}

	void onDestroyWindow(HWND hwnd)
	{
		if (isTopLevelWindow(hwnd))
		{
			Gdi::Window::updateAll();
			return;
		}

		Compat::ScopedSrwLockExclusive lock(g_windowProcSrwLock);
		auto it = g_windowProc.find(hwnd);
		if (it != g_windowProc.end())
		{
			setWindowProc(hwnd, it->second.wndProcA, it->second.wndProcW);
			g_windowProc.erase(it);
		}
	}

	void onGetMinMaxInfo(MINMAXINFO& mmi)
	{
		MONITORINFOEXA mi = {};
		mi.cbSize = sizeof(mi);
		GetMonitorInfoA(MonitorFromPoint({}, MONITOR_DEFAULTTOPRIMARY), &mi);
		mmi.ptMaxSize.x = mi.rcMonitor.right - 2 * mmi.ptMaxPosition.x;
		mmi.ptMaxSize.y = mi.rcMonitor.bottom - 2 * mmi.ptMaxPosition.y;
	}

	void onInitMenuPopup(HMENU menu)
	{
		if (Gdi::Cursor::isEmulated())
		{
			MENUINFO mi = {};
			mi.cbSize = sizeof(mi);
			mi.fMask = MIM_MAXHEIGHT;
			GetMenuInfo(menu, &mi);

			RECT mr = DDraw::PrimarySurface::getMonitorRect();
			UINT maxHeight = mr.bottom - mr.top;
			if (0 == mi.cyMax || mi.cyMax > maxHeight)
			{
				g_menuMaxHeight[menu] = mi.cyMax;
				mi.cyMax = maxHeight;
				SetMenuInfo(menu, &mi);
			}
		}
	}

	void onUninitMenuPopup(HMENU menu)
	{
		auto it = g_menuMaxHeight.find(menu);
		if (it != g_menuMaxHeight.end())
		{
			MENUINFO mi = {};
			mi.cbSize = sizeof(mi);
			mi.fMask = MIM_MAXHEIGHT;
			mi.cyMax = it->second;
			SetMenuInfo(menu, &mi);
			g_menuMaxHeight.erase(it);
		}
	}

	void onWindowPosChanged(HWND hwnd, const WINDOWPOS& wp)
	{
		for (auto notifyFunc : g_windowPosChangeNotifyFuncs)
		{
			notifyFunc();
		}

		if (isTopLevelWindow(hwnd))
		{
			Gdi::Window::updateAll();
		}

		if (wp.flags & SWP_FRAMECHANGED)
		{
			RECT r = { -1, -1, 0, 0 };
			RedrawWindow(hwnd, &r, nullptr, RDW_INVALIDATE | RDW_FRAME);
		}
	}

	void onWindowPosChanging(HWND hwnd, WINDOWPOS& wp)
	{
		if (isTopLevelWindow(hwnd))
		{
			wp.flags |= SWP_NOREDRAW;
		}
		else
		{
			wp.flags |= SWP_NOCOPYBITS;
		}
	}

	BOOL WINAPI setLayeredWindowAttributes(HWND hwnd, COLORREF crKey, BYTE bAlpha, DWORD dwFlags)
	{
		LOG_FUNC("SetLayeredWindowAttributes", hwnd, crKey, bAlpha, dwFlags);
		BOOL result = CALL_ORIG_FUNC(SetLayeredWindowAttributes)(hwnd, crKey, bAlpha, dwFlags);
		if (result && DDraw::RealPrimarySurface::isFullscreen())
		{
			DDraw::RealPrimarySurface::scheduleUpdate();
		}
		return LOG_RESULT(result);
	}

	LONG setWindowLong(HWND hWnd, int nIndex, LONG dwNewLong,
		decltype(&SetWindowLongA) origSetWindowLong, WNDPROC(WindowProc::* wndProc))
	{
		if (GWL_WNDPROC == nIndex)
		{
			Compat::ScopedSrwLockExclusive lock(g_windowProcSrwLock);
			auto it = g_windowProc.find(hWnd);
			if (it != g_windowProc.end() && 0 != origSetWindowLong(hWnd, nIndex, dwNewLong))
			{
				WNDPROC oldWndProc = it->second.*wndProc;
				it->second.wndProcA = reinterpret_cast<WNDPROC>(CALL_ORIG_FUNC(GetWindowLongA)(hWnd, GWL_WNDPROC));
				it->second.wndProcW = reinterpret_cast<WNDPROC>(CALL_ORIG_FUNC(GetWindowLongW)(hWnd, GWL_WNDPROC));
				WindowProc newWindowProc = { ddcWindowProcA, ddcWindowProcW };
				origSetWindowLong(hWnd, GWL_WNDPROC, reinterpret_cast<LONG>(newWindowProc.*wndProc));
				return reinterpret_cast<LONG>(oldWndProc);
			}
		}
		return origSetWindowLong(hWnd, nIndex, dwNewLong);
	}

	LONG WINAPI setWindowLongA(HWND hWnd, int nIndex, LONG dwNewLong)
	{
		LOG_FUNC("SetWindowLongA", hWnd, nIndex, dwNewLong);
		return LOG_RESULT(setWindowLong(hWnd, nIndex, dwNewLong, CALL_ORIG_FUNC(SetWindowLongA), &WindowProc::wndProcA));
	}

	LONG WINAPI setWindowLongW(HWND hWnd, int nIndex, LONG dwNewLong)
	{
		LOG_FUNC("SetWindowLongW", hWnd, nIndex, dwNewLong);
		return LOG_RESULT(setWindowLong(hWnd, nIndex, dwNewLong, CALL_ORIG_FUNC(SetWindowLongW), &WindowProc::wndProcW));
	}

	BOOL WINAPI setWindowPos(HWND hWnd, HWND hWndInsertAfter, int X, int Y, int cx, int cy, UINT uFlags)
	{
		LOG_FUNC("SetWindowPos", hWnd, hWndInsertAfter, X, Y, cx, cy, Compat::hex(uFlags));
		if (uFlags & SWP_NOSENDCHANGING)
		{
			WINDOWPOS wp = {};
			wp.hwnd = hWnd;
			wp.hwndInsertAfter = hWndInsertAfter;
			wp.x = X;
			wp.y = Y;
			wp.cx = cx;
			wp.cy = cy;
			wp.flags = uFlags;
			onWindowPosChanging(hWnd, wp);
			uFlags = wp.flags;
		}
		return LOG_RESULT(CALL_ORIG_FUNC(SetWindowPos)(hWnd, hWndInsertAfter, X, Y, cx, cy, uFlags));
	}

	void setWindowProc(HWND hwnd, WNDPROC wndProcA, WNDPROC wndProcW)
	{
		if (IsWindowUnicode(hwnd))
		{
			CALL_ORIG_FUNC(SetWindowLongW)(hwnd, GWL_WNDPROC, reinterpret_cast<LONG>(wndProcW));
		}
		else
		{
			CALL_ORIG_FUNC(SetWindowLongA)(hwnd, GWL_WNDPROC, reinterpret_cast<LONG>(wndProcA));
		}
	}

	void CALLBACK winEventProc(
		HWINEVENTHOOK /*hWinEventHook*/,
		DWORD event,
		HWND hwnd,
		LONG idObject,
		LONG /*idChild*/,
		DWORD /*dwEventThread*/,
		DWORD /*dwmsEventTime*/)
	{
		LOG_FUNC("winEventProc", Compat::hex(event), hwnd, idObject);

		switch (event)
		{
		case EVENT_OBJECT_CREATE:
			if (OBJID_WINDOW == idObject && !Gdi::PresentationWindow::isPresentationWindow(hwnd))
			{
				onCreateWindow(hwnd);
			}
			break;

		case EVENT_OBJECT_NAMECHANGE:
		case EVENT_OBJECT_SHOW:
		case EVENT_OBJECT_HIDE:
			if (OBJID_CURSOR == idObject && Gdi::Cursor::isEmulated())
			{
				Gdi::Cursor::setCursor(CALL_ORIG_FUNC(GetCursor)());
			}
			break;

		case EVENT_OBJECT_STATECHANGE:
			switch (idObject)
			{
			case OBJID_TITLEBAR:
			{
				HDC dc = GetWindowDC(hwnd);
				Gdi::TitleBar(hwnd).drawButtons(dc);
				ReleaseDC(hwnd, dc);
				break;
			}

			case OBJID_CLIENT:
				if (!isUser32ScrollBar(hwnd))
				{
					break;
				}
			case OBJID_HSCROLL:
			case OBJID_VSCROLL:
			{
				HDC dc = GetWindowDC(hwnd);
				if (OBJID_CLIENT == idObject)
				{
					SendMessage(GetParent(hwnd), WM_CTLCOLORSCROLLBAR,
						reinterpret_cast<WPARAM>(dc), reinterpret_cast<LPARAM>(hwnd));
				}
				else
				{
					DefWindowProc(hwnd, WM_CTLCOLORSCROLLBAR,
						reinterpret_cast<WPARAM>(dc), reinterpret_cast<LPARAM>(hwnd));
				}
				ReleaseDC(hwnd, dc);
				break;
			}
			}
		}
	}
}

namespace Gdi
{
	namespace WinProc
	{
		void dllThreadDetach()
		{
			auto threadId = GetCurrentThreadId();
			Compat::ScopedSrwLockExclusive lock(g_windowProcSrwLock);
			auto it = g_windowProc.begin();
			while (it != g_windowProc.end())
			{
				if (threadId == GetWindowThreadProcessId(it->first, nullptr))
				{
					it = g_windowProc.erase(it);
				}
				else
				{
					++it;
				}
			}
		}

		void installHooks()
		{
			HOOK_FUNCTION(user32, GetWindowLongA, getWindowLongA);
			HOOK_FUNCTION(user32, GetWindowLongW, getWindowLongW);
			HOOK_FUNCTION(user32, SetLayeredWindowAttributes, setLayeredWindowAttributes);
			HOOK_FUNCTION(user32, SetWindowLongA, setWindowLongA);
			HOOK_FUNCTION(user32, SetWindowLongW, setWindowLongW);
			HOOK_FUNCTION(user32, SetWindowPos, setWindowPos);

			SetWinEventHook(EVENT_OBJECT_CREATE, EVENT_OBJECT_CREATE,
				Dll::g_currentModule, &winEventProc, GetCurrentProcessId(), 0, WINEVENT_INCONTEXT);
			SetWinEventHook(EVENT_OBJECT_NAMECHANGE, EVENT_OBJECT_NAMECHANGE,
				Dll::g_currentModule, &winEventProc, GetCurrentProcessId(), 0, WINEVENT_INCONTEXT);
			SetWinEventHook(EVENT_OBJECT_SHOW, EVENT_OBJECT_HIDE,
				Dll::g_currentModule, &winEventProc, GetCurrentProcessId(), 0, WINEVENT_INCONTEXT);
			SetWinEventHook(EVENT_OBJECT_STATECHANGE, EVENT_OBJECT_STATECHANGE,
				Dll::g_currentModule, &winEventProc, GetCurrentProcessId(), 0, WINEVENT_INCONTEXT);
		}

		void onCreateWindow(HWND hwnd)
		{
			if (PresentationWindow::isThreadReady())
			{
				::onCreateWindow(hwnd);
			}
		}

		void watchWindowPosChanges(WindowPosChangeNotifyFunc notifyFunc)
		{
			g_windowPosChangeNotifyFuncs.insert(notifyFunc);
		}
	}
}
