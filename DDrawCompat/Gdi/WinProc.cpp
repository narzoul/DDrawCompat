#include <map>
#include <set>

#include <Windows.h>

#include <Common/Hook.h>
#include <Common/Log.h>
#include <Common/ScopedCriticalSection.h>
#include <Dll/Dll.h>
#include <Gdi/AccessGuard.h>
#include <Gdi/Dc.h>
#include <Win32/DisplayMode.h>
#include <Gdi/ScrollBar.h>
#include <Gdi/ScrollFunctions.h>
#include <Gdi/TitleBar.h>
#include <Gdi/Window.h>
#include <Gdi/WinProc.h>

namespace
{
	const char* PROP_DDRAWCOMPAT = "DDrawCompat";

	struct ChildWindowInfo
	{
		RECT rect;
		Gdi::Region visibleRegion;

		ChildWindowInfo() : rect{} {}
	};

	struct WindowProc
	{
		WNDPROC wndProcA;
		WNDPROC wndProcW;
	};

	HWINEVENTHOOK g_objectCreateEventHook = nullptr;
	HWINEVENTHOOK g_objectStateChangeEventHook = nullptr;
	std::set<Gdi::WindowPosChangeNotifyFunc> g_windowPosChangeNotifyFuncs;

	Compat::CriticalSection g_windowProcCs;
	std::map<HWND, WindowProc> g_windowProc;

	WindowProc getWindowProc(HWND hwnd);
	void onActivate(HWND hwnd);
	void onCreateWindow(HWND hwnd);
	void onDestroyWindow(HWND hwnd);
	void onWindowPosChanged(HWND hwnd);
	void onWindowPosChanging(HWND hwnd, const WINDOWPOS& wp);
	void setWindowProc(HWND hwnd, WNDPROC wndProcA, WNDPROC wndProcW);

	LRESULT CALLBACK ddcWindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam,
		decltype(&CallWindowProcA) callWindowProc, WNDPROC wndProc)
	{
		LOG_FUNC("ddcWindowProc", Compat::WindowMessageStruct(hwnd, uMsg, wParam, lParam));

		switch (uMsg)
		{
		case WM_WINDOWPOSCHANGED:
			onWindowPosChanged(hwnd);
			break;
		}

		LRESULT result = callWindowProc(wndProc, hwnd, uMsg, wParam, lParam);

		switch (uMsg)
		{
		case WM_ACTIVATE:
			onActivate(hwnd);
			break;

		case WM_COMMAND:
		{
			auto notifCode = HIWORD(wParam);
			if (lParam && (EN_HSCROLL == notifCode || EN_VSCROLL == notifCode))
			{
				Gdi::ScrollFunctions::updateScrolledWindow(reinterpret_cast<HWND>(lParam));
			}
			break;
		}

		case WM_NCDESTROY:
			onDestroyWindow(hwnd);
			break;

		case WM_STYLECHANGED:
			if (GWL_EXSTYLE == wParam)
			{
				onWindowPosChanged(hwnd);
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
			Compat::ScopedCriticalSection lock(g_windowProcCs);
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
		Compat::ScopedCriticalSection lock(g_windowProcCs);
		return g_windowProc[hwnd];
	}

	BOOL CALLBACK initChildWindow(HWND hwnd, LPARAM /*lParam*/)
	{
		onCreateWindow(hwnd);
		return TRUE;
	}

	BOOL CALLBACK initTopLevelWindow(HWND hwnd, LPARAM /*lParam*/)
	{
		DWORD windowPid = 0;
		GetWindowThreadProcessId(hwnd, &windowPid);
		if (GetCurrentProcessId() == windowPid)
		{
			onCreateWindow(hwnd);
			EnumChildWindows(hwnd, &initChildWindow, 0);
			if (8 == Win32::DisplayMode::getBpp())
			{
				PostMessage(hwnd, WM_PALETTECHANGED, reinterpret_cast<WPARAM>(GetDesktopWindow()), 0);
			}
		}
		return TRUE;
	}

	void CALLBACK objectCreateEvent(
		HWINEVENTHOOK /*hWinEventHook*/,
		DWORD /*event*/,
		HWND hwnd,
		LONG idObject,
		LONG /*idChild*/,
		DWORD /*dwEventThread*/,
		DWORD /*dwmsEventTime*/)
	{
		if (OBJID_WINDOW == idObject && !Gdi::Window::isPresentationWindow(hwnd))
		{
			onCreateWindow(hwnd);
		}
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
				Gdi::AccessGuard accessGuard(Gdi::ACCESS_WRITE);
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
			CALL_ORIG_FUNC(ReleaseDC)(hwnd, windowDc);
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
		char className[64] = {};
		GetClassName(hwnd, className, sizeof(className));
		if (std::string(className) == "CompatWindowDesktopReplacement")
		{
			// Disable VirtualizeDesktopPainting shim
			SendNotifyMessage(hwnd, WM_CLOSE, 0, 0);
			return;
		}

		if (!Gdi::Window::isPresentationWindow(hwnd))
		{
			Compat::ScopedCriticalSection lock(g_windowProcCs);
			if (g_windowProc.find(hwnd) == g_windowProc.end())
			{
				auto wndProcA = reinterpret_cast<WNDPROC>(CALL_ORIG_FUNC(GetWindowLongA)(hwnd, GWL_WNDPROC));
				auto wndProcW = reinterpret_cast<WNDPROC>(CALL_ORIG_FUNC(GetWindowLongW)(hwnd, GWL_WNDPROC));
				g_windowProc[hwnd] = { wndProcA, wndProcW };
				setWindowProc(hwnd, ddcWindowProcA, ddcWindowProcW);
			}
		}

		Gdi::Window::add(hwnd);
	}

	void onDestroyWindow(HWND hwnd)
	{
		Gdi::Window::remove(hwnd);
		delete reinterpret_cast<ChildWindowInfo*>(RemoveProp(hwnd, PROP_DDRAWCOMPAT));

		Compat::ScopedCriticalSection lock(g_windowProcCs);
		auto it = g_windowProc.find(hwnd);
		if (it != g_windowProc.end())
		{
			setWindowProc(hwnd, it->second.wndProcA, it->second.wndProcW);
			g_windowProc.erase(it);
		}
	}

	void onWindowPosChanged(HWND hwnd)
	{
		if (Gdi::MENU_ATOM == GetClassLongPtr(hwnd, GCW_ATOM))
		{
			auto exStyle = CALL_ORIG_FUNC(GetWindowLongA)(hwnd, GWL_EXSTYLE);
			if (exStyle & WS_EX_LAYERED)
			{
				CALL_ORIG_FUNC(SetWindowLongA)(hwnd, GWL_EXSTYLE, exStyle & ~WS_EX_LAYERED);
				return;
			}
		}

		for (auto notifyFunc : g_windowPosChangeNotifyFuncs)
		{
			notifyFunc();
		}

		if (Gdi::Window::isTopLevelWindow(hwnd))
		{
			Gdi::Window::add(hwnd);
			Gdi::Window::updateAll();
		}
		else
		{
			std::unique_ptr<ChildWindowInfo> cwi(reinterpret_cast<ChildWindowInfo*>(RemoveProp(hwnd, PROP_DDRAWCOMPAT)));
			if (cwi && IsWindowVisible(hwnd) && !IsIconic(GetAncestor(hwnd, GA_ROOT)))
			{
				RECT rect = {};
				GetWindowRect(hwnd, &rect);
				if (rect.left != cwi->rect.left || rect.top != cwi->rect.top)
				{
					Gdi::Region clipRegion(hwnd);
					cwi->visibleRegion.offset(rect.left - cwi->rect.left, rect.top - cwi->rect.top);
					clipRegion &= cwi->visibleRegion;

					Gdi::Region updateRegion;
					GetUpdateRgn(hwnd, updateRegion, FALSE);
					POINT clientPos = {};
					ClientToScreen(hwnd, &clientPos);
					OffsetRgn(updateRegion, clientPos.x, clientPos.y);
					clipRegion -= updateRegion;

					if (!clipRegion.isEmpty())
					{
						HDC screenDc = GetDC(nullptr);
						SelectClipRgn(screenDc, clipRegion);
						BitBlt(screenDc, rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top,
							screenDc, cwi->rect.left, cwi->rect.top, SRCCOPY);
						SelectClipRgn(screenDc, nullptr);
						CALL_ORIG_FUNC(ReleaseDC)(nullptr, screenDc);
					}
				}
			}
		}
	}

	void onWindowPosChanging(HWND hwnd, const WINDOWPOS& wp)
	{
		if (!Gdi::Window::isTopLevelWindow(hwnd))
		{
			std::unique_ptr<ChildWindowInfo> cwi(reinterpret_cast<ChildWindowInfo*>(RemoveProp(hwnd, PROP_DDRAWCOMPAT)));
			if (!(wp.flags & SWP_NOMOVE) && IsWindowVisible(hwnd) && !IsIconic(GetAncestor(hwnd, GA_ROOT)))
			{
				cwi.reset(new ChildWindowInfo());
				GetWindowRect(hwnd, &cwi->rect);
				cwi->visibleRegion = hwnd;
				if (!cwi->visibleRegion.isEmpty())
				{
					SetProp(hwnd, PROP_DDRAWCOMPAT, cwi.release());
				}
			}
		}
	}

	LONG setWindowLong(HWND hWnd, int nIndex, LONG dwNewLong,
		decltype(&SetWindowLongA) origSetWindowLong, WNDPROC(WindowProc::* wndProc))
	{
		if (GWL_WNDPROC == nIndex)
		{
			Compat::ScopedCriticalSection lock(g_windowProcCs);
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
		}
		BOOL result = CALL_ORIG_FUNC(SetWindowPos)(hWnd, hWndInsertAfter, X, Y, cx, cy, uFlags);
		delete reinterpret_cast<ChildWindowInfo*>(RemoveProp(hWnd, PROP_DDRAWCOMPAT));
		return LOG_RESULT(result);
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
}

namespace Gdi
{
	namespace WinProc
	{
		void dllThreadDetach()
		{
			auto threadId = GetCurrentThreadId();
			Compat::ScopedCriticalSection lock(g_windowProcCs);
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
			HOOK_FUNCTION(user32, SetWindowLongA, setWindowLongA);
			HOOK_FUNCTION(user32, SetWindowLongW, setWindowLongW);
			HOOK_FUNCTION(user32, SetWindowPos, setWindowPos);

			g_objectCreateEventHook = SetWinEventHook(EVENT_OBJECT_CREATE, EVENT_OBJECT_CREATE,
				Dll::g_currentModule, &objectCreateEvent, GetCurrentProcessId(), 0, WINEVENT_INCONTEXT);
			g_objectStateChangeEventHook = SetWinEventHook(EVENT_OBJECT_STATECHANGE, EVENT_OBJECT_STATECHANGE,
				Dll::g_currentModule, &objectStateChangeEvent, GetCurrentProcessId(), 0, WINEVENT_INCONTEXT);

			EnumWindows(initTopLevelWindow, 0);
			Gdi::Window::updateAll();
		}

		void watchWindowPosChanges(WindowPosChangeNotifyFunc notifyFunc)
		{
			g_windowPosChangeNotifyFuncs.insert(notifyFunc);
		}

		void uninstallHooks()
		{
			UnhookWinEvent(g_objectStateChangeEventHook);
			UnhookWinEvent(g_objectCreateEventHook);

			Compat::ScopedCriticalSection lock(g_windowProcCs);
			for (const auto& windowProc : g_windowProc)
			{
				setWindowProc(windowProc.first, windowProc.second.wndProcA, windowProc.second.wndProcW);
			}
			g_windowProc.clear();
		}
	}
}
