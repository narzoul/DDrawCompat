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
#include <Gdi/PaintHandlers.h>
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

	std::multimap<DWORD, HHOOK> g_threadIdToHook;
	Compat::CriticalSection g_threadIdToHookCs;
	HWINEVENTHOOK g_objectCreateEventHook = nullptr;
	HWINEVENTHOOK g_objectStateChangeEventHook = nullptr;
	std::set<Gdi::WindowPosChangeNotifyFunc> g_windowPosChangeNotifyFuncs;

	void onActivate(HWND hwnd);
	void onCreateWindow(HWND hwnd);
	void onDestroyWindow(HWND hwnd);
	void onWindowPosChanged(HWND hwnd);
	void onWindowPosChanging(HWND hwnd, const WINDOWPOS& wp);

	LRESULT CALLBACK callWndRetProc(int nCode, WPARAM wParam, LPARAM lParam)
	{
		auto ret = reinterpret_cast<CWPRETSTRUCT*>(lParam);
		LOG_FUNC("callWndRetProc", Compat::hex(nCode), Compat::hex(wParam), ret);

		if (HC_ACTION == nCode && !Gdi::Window::isPresentationWindow(ret->hwnd))
		{
			switch (ret->message)
			{
			case WM_ACTIVATE:
				onActivate(ret->hwnd);
				break;

			case WM_COMMAND:
			{
				auto notifCode = HIWORD(ret->wParam);
				if (ret->lParam && (EN_HSCROLL == notifCode || EN_VSCROLL == notifCode))
				{
					Gdi::ScrollFunctions::updateScrolledWindow(reinterpret_cast<HWND>(ret->lParam));
				}
				break;
			}

			case WM_DESTROY:
				onDestroyWindow(ret->hwnd);
				break;
			
			case WM_STYLECHANGED:
				if (GWL_EXSTYLE == ret->wParam)
				{
					onWindowPosChanged(ret->hwnd);
				}
				break;

			case WM_WINDOWPOSCHANGED:
				onWindowPosChanged(ret->hwnd);
				break;

			case WM_WINDOWPOSCHANGING:
				onWindowPosChanging(ret->hwnd, *reinterpret_cast<WINDOWPOS*>(ret->lParam));
				break;
			}
		}

		return LOG_RESULT(CallNextHookEx(nullptr, nCode, wParam, lParam));
	}

	void hookThread(DWORD threadId)
	{
		Compat::ScopedCriticalSection lock(g_threadIdToHookCs);
		if (g_threadIdToHook.end() == g_threadIdToHook.find(threadId))
		{
			g_threadIdToHook.emplace(threadId,
				SetWindowsHookEx(WH_CALLWNDPROCRET, callWndRetProc, nullptr, threadId));
		}
	}

	BOOL CALLBACK initTopLevelWindow(HWND hwnd, LPARAM /*lParam*/)
	{
		DWORD windowPid = 0;
		GetWindowThreadProcessId(hwnd, &windowPid);
		if (GetCurrentProcessId() == windowPid)
		{
			onCreateWindow(hwnd);
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

		hookThread(GetWindowThreadProcessId(hwnd, nullptr));
		Gdi::PaintHandlers::onCreateWindow(hwnd);
		Gdi::Window::add(hwnd);
	}

	void onDestroyWindow(HWND hwnd)
	{
		Gdi::Window::remove(hwnd);
		delete reinterpret_cast<ChildWindowInfo*>(RemoveProp(hwnd, PROP_DDRAWCOMPAT));
	}

	void onWindowPosChanged(HWND hwnd)
	{
		if (Gdi::MENU_ATOM == GetClassLongPtr(hwnd, GCW_ATOM))
		{
			auto exStyle = GetWindowLongPtr(hwnd, GWL_EXSTYLE);
			if (exStyle & WS_EX_LAYERED)
			{
				SetWindowLongPtr(hwnd, GWL_EXSTYLE, exStyle & ~WS_EX_LAYERED);
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
}

namespace Gdi
{
	namespace WinProc
	{
		void dllThreadDetach()
		{
			Compat::ScopedCriticalSection lock(g_threadIdToHookCs);
			const DWORD threadId = GetCurrentThreadId();
			for (auto threadIdToHook : g_threadIdToHook)
			{
				if (threadId == threadIdToHook.first)
				{
					UnhookWindowsHookEx(threadIdToHook.second);
				}
			}
			g_threadIdToHook.erase(threadId);
		}

		void installHooks()
		{
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

			{
				Compat::ScopedCriticalSection lock(g_threadIdToHookCs);
				for (auto threadIdToHook : g_threadIdToHook)
				{
					UnhookWindowsHookEx(threadIdToHook.second);
				}
				g_threadIdToHook.clear();
			}
		}
	}
}
