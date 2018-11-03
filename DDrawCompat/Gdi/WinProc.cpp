#define WIN32_LEAN_AND_MEAN

#include <map>
#include <set>

#include <dwmapi.h>
#include <Windows.h>

#include "Common/Log.h"
#include "Common/ScopedCriticalSection.h"
#include "Gdi/AccessGuard.h"
#include "Gdi/Dc.h"
#include "Gdi/PaintHandlers.h"
#include "Gdi/ScrollBar.h"
#include "Gdi/ScrollFunctions.h"
#include "Gdi/TitleBar.h"
#include "Gdi/Window.h"
#include "Gdi/WinProc.h"

extern "C" IMAGE_DOS_HEADER __ImageBase;

namespace
{
	std::multimap<DWORD, HHOOK> g_threadIdToHook;
	Compat::CriticalSection g_threadIdToHookCs;
	HWINEVENTHOOK g_objectCreateEventHook = nullptr;
	HWINEVENTHOOK g_objectStateChangeEventHook = nullptr;
	std::set<Gdi::WindowPosChangeNotifyFunc> g_windowPosChangeNotifyFuncs;

	void disableDwmAttributes(HWND hwnd);
	void onActivate(HWND hwnd);
	void onCreateWindow(HWND hwnd);
	void onDestroyWindow(HWND hwnd);
	void onWindowPosChanged(HWND hwnd);
	void removeDropShadow(HWND hwnd);

	LRESULT CALLBACK callWndRetProc(int nCode, WPARAM wParam, LPARAM lParam)
	{
		auto ret = reinterpret_cast<CWPRETSTRUCT*>(lParam);
		LOG_FUNC("callWndRetProc", Compat::hex(nCode), Compat::hex(wParam), ret);

		if (HC_ACTION == nCode && !Gdi::Window::isPresentationWindow(ret->hwnd))
		{
			if (WM_DESTROY == ret->message)
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
		}

		return LOG_RESULT(CallNextHookEx(nullptr, nCode, wParam, lParam));
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
		if (GetCurrentProcessId() != windowPid)
		{
			return TRUE;
		}

		onCreateWindow(hwnd);
		if (!(GetWindowLongPtr(hwnd, GWL_EXSTYLE) & WS_EX_LAYERED))
		{
			RedrawWindow(hwnd, nullptr, nullptr,
				RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN | RDW_UPDATENOW);
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
		hookThread(GetWindowThreadProcessId(hwnd, nullptr));
		disableDwmAttributes(hwnd);
		removeDropShadow(hwnd);
		Gdi::PaintHandlers::onCreateWindow(hwnd);
		if (Gdi::Window::isTopLevelNonLayeredWindow(hwnd))
		{
			Gdi::Window::add(hwnd);
		}
	}

	void onDestroyWindow(HWND hwnd)
	{
		Gdi::Window::remove(hwnd);
	}

	void onWindowPosChanged(HWND hwnd)
	{
		if (Gdi::MENU_ATOM == GetClassLongPtr(hwnd, GCW_ATOM))
		{
			SetWindowLongPtr(hwnd, GWL_EXSTYLE, GetWindowLongPtr(hwnd, GWL_EXSTYLE) & ~WS_EX_LAYERED);
		}

		for (auto notifyFunc : g_windowPosChangeNotifyFuncs)
		{
			notifyFunc();
		}

		if (Gdi::Window::get(hwnd))
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
			g_objectCreateEventHook = SetWinEventHook(EVENT_OBJECT_CREATE, EVENT_OBJECT_CREATE,
				reinterpret_cast<HMODULE>(&__ImageBase), &objectCreateEvent,
				GetCurrentProcessId(), 0, WINEVENT_INCONTEXT);
			g_objectStateChangeEventHook = SetWinEventHook(EVENT_OBJECT_STATECHANGE, EVENT_OBJECT_STATECHANGE,
				reinterpret_cast<HMODULE>(&__ImageBase), &objectStateChangeEvent,
				GetCurrentProcessId(), 0, WINEVENT_INCONTEXT);

			EnumWindows(initTopLevelWindow, 0);
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
