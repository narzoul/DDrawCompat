#include <map>
#include <set>

#include <Windows.h>

#include <Common/Hook.h>
#include <Common/Log.h>
#include <Common/ScopedSrwLock.h>
#include <Common/ScopedThreadPriority.h>
#include <Common/Time.h>
#include <Config/Config.h>
#include <Dll/Dll.h>
#include <DDraw/DirectDraw.h>
#include <DDraw/RealPrimarySurface.h>
#include <DDraw/Surfaces/PrimarySurface.h>
#include <DDraw/Surfaces/TagSurface.h>
#include <Gdi/CompatDc.h>
#include <Gdi/Cursor.h>
#include <Gdi/Dc.h>
#include <Gdi/Gdi.h>
#include <Gdi/GuiThread.h>
#include <Gdi/PresentationWindow.h>
#include <Gdi/ScrollBar.h>
#include <Gdi/ScrollFunctions.h>
#include <Gdi/TitleBar.h>
#include <Gdi/Window.h>
#include <Gdi/WinProc.h>
#include <Overlay/ConfigWindow.h>
#include <Overlay/StatsWindow.h>
#include <Win32/DisplayMode.h>

namespace
{
	class ScopedIncrement
	{
	public:
		ScopedIncrement(unsigned& num) : m_num(num) { ++m_num; }
		~ScopedIncrement() { --m_num; }

	private:
		unsigned& m_num;
	};

	struct WindowProc
	{
		WNDPROC wndProcA;
		WNDPROC wndProcW;
	};

	std::map<HMENU, UINT> g_menuMaxHeight;
	std::set<Gdi::WindowPosChangeNotifyFunc> g_windowPosChangeNotifyFuncs;

	Compat::SrwLock g_windowProcSrwLock;
	std::map<HWND, WindowProc> g_windowProc;

	thread_local unsigned g_inCreateDialog = 0;
	thread_local unsigned g_inMessageBox = 0;
	thread_local unsigned g_inWindowProc = 0;
	thread_local long long g_qpcWaitEnd = 0;
	thread_local bool g_isFrameStarted = false;
	thread_local bool g_waiting = false;

	WindowProc getWindowProc(HWND hwnd);
	bool isUser32ScrollBar(HWND hwnd);
	void onDestroyWindow(HWND hwnd);
	void onGetMinMaxInfo(MINMAXINFO& mmi);
	void onInitDialog(HWND hwnd);
	void onInitMenuPopup(HMENU menu);
	void onUninitMenuPopup(HMENU menu);
	void onWindowPosChanged(HWND hwnd, const WINDOWPOS& wp);
	void onWindowPosChanging(HWND hwnd, WINDOWPOS& wp);
	LONG WINAPI setWindowLongA(HWND hWnd, int nIndex, LONG dwNewLong);
	void setWindowProc(HWND hwnd, WNDPROC wndProcA, WNDPROC wndProcW);

	template <auto func, typename Result, typename... Params>
	Result WINAPI createDialog(Params... params)
	{
		LOG_FUNC(Compat::g_origFuncName<func>.c_str(), params...);
		++g_inCreateDialog;
		Result result = CALL_ORIG_FUNC(func)(params...);
		--g_inCreateDialog;
		return LOG_RESULT(result);
	}

	LRESULT CALLBACK ddcWindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam,
		decltype(&CallWindowProcA) callWindowProc, WNDPROC wndProc)
	{
		LOG_FUNC("ddcWindowProc", Compat::WindowMessageStruct(hwnd, uMsg, wParam, lParam));
		ScopedIncrement inc(g_inWindowProc);

		switch (uMsg)
		{
		case WM_DISPLAYCHANGE:
			if (0 != wParam)
			{
				return 0;
			}
			wParam = Win32::DisplayMode::getBpp();
			break;

		case WM_DWMCOMPOSITIONCHANGED:
			Gdi::checkDesktopComposition();
			break;

		case WM_GETMINMAXINFO:
			onGetMinMaxInfo(*reinterpret_cast<MINMAXINFO*>(lParam));
			break;

		case WM_INITDIALOG:
			onInitDialog(hwnd);
			break;

		case WM_SYNCPAINT:
			if (Gdi::Window::isTopLevelWindow(hwnd))
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

		LRESULT result = 0;
		if (WM_ACTIVATEAPP == uMsg && Dll::g_origDDrawModule == Compat::getModuleHandleFromAddress(
			reinterpret_cast<void*>(GetWindowLongA(hwnd, GWL_WNDPROC))))
		{
			result = DDraw::DirectDraw::handleActivateApp(wParam, [=]() {
				return callWindowProc(wndProc, hwnd, uMsg, wParam, lParam); });
		}
		else
		{
			result = callWindowProc(wndProc, hwnd, uMsg, wParam, lParam);
		}

		switch (uMsg)
		{
		case WM_ACTIVATEAPP:
			Gdi::GuiThread::execute([&]()
				{
					static bool hidden = false;
					static bool configVisible = false;
					static bool statsVisible = false;

					auto configWindow = Gdi::GuiThread::getConfigWindow();
					auto statsWindow = Gdi::GuiThread::getStatsWindow();
					if (!wParam && !hidden)
					{
						configVisible = configWindow ? configWindow->isVisible() : false;
						statsVisible = statsWindow ? statsWindow->isVisible() : false;
						hidden = true;
					}

					if (configWindow)
					{
						configWindow->setVisible(wParam ? configVisible : false);
					}
					if (statsWindow)
					{
						statsWindow->setVisible(wParam ? statsVisible : false);
					}

					if (wParam)
					{
						hidden = false;
					}
					else
					{
						CALL_ORIG_FUNC(ClipCursor)(nullptr);
					}
				});
			break;

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

		case WM_SETCURSOR:
			SetCursor(GetCursor());
			break;

		case WM_STYLECHANGED:
			if (Gdi::Window::isTopLevelWindow(hwnd))
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

	LONG WINAPI ddrawSetWindowLongA(HWND hWnd, int nIndex, LONG dwNewLong)
	{
		LOG_FUNC("ddrawSetWindowLongA", hWnd, nIndex, dwNewLong);
		if (GWL_WNDPROC == nIndex)
		{
			return setWindowLongA(hWnd, GWL_WNDPROC, dwNewLong);
		}

		if (GWL_STYLE == nIndex)
		{
			auto style = CALL_ORIG_FUNC(GetWindowLongA)(hWnd, GWL_STYLE);
			if (style & WS_CLIPCHILDREN)
			{
				dwNewLong = style | WS_CLIPCHILDREN;
				if (dwNewLong == style)
				{
					return LOG_RESULT(style);
				}
			}
		}

		return LOG_RESULT(CALL_ORIG_FUNC(SetWindowLongA)(hWnd, nIndex, dwNewLong));
	}

	BOOL WINAPI getMessage(LPMSG lpMsg, HWND hWnd, UINT wMsgFilterMin, UINT wMsgFilterMax,
		decltype(&GetMessageA) origGetMessage)
	{
		DDraw::RealPrimarySurface::setUpdateReady();
		return origGetMessage(lpMsg, hWnd, wMsgFilterMin, wMsgFilterMax);
	}

	BOOL WINAPI getMessageA(LPMSG lpMsg, HWND hWnd, UINT wMsgFilterMin, UINT wMsgFilterMax)
	{
		return getMessage(lpMsg, hWnd, wMsgFilterMin, wMsgFilterMax, CALL_ORIG_FUNC(GetMessageA));
	}

	BOOL WINAPI getMessageW(LPMSG lpMsg, HWND hWnd, UINT wMsgFilterMin, UINT wMsgFilterMax)
	{
		return getMessage(lpMsg, hWnd, wMsgFilterMin, wMsgFilterMax, CALL_ORIG_FUNC(GetMessageW));
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

	template <auto func, typename... Params>
	int WINAPI messageBox(Params... params)
	{
		LOG_FUNC(Compat::g_origFuncName<func>.c_str(), params...);
		++g_inMessageBox;
		int result = CALL_ORIG_FUNC(func)(params...);
		--g_inMessageBox;
		return LOG_RESULT(result);
	}

	void onInitDialog(HWND hwnd)
	{
		if (!Gdi::Window::isTopLevelWindow(hwnd) ||
			0 == g_inMessageBox &&
			(0 == g_inCreateDialog || !(CALL_ORIG_FUNC(GetWindowLongA)(hwnd, GWL_STYLE) & DS_CENTER)))
		{
			return;
		}

		HMONITOR monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTOPRIMARY);

		MONITORINFO origMi = {};
		origMi.cbSize = sizeof(origMi);
		CALL_ORIG_FUNC(GetMonitorInfoA)(monitor, &origMi);

		MONITORINFO mi = {};
		mi.cbSize = sizeof(mi);
		GetMonitorInfoA(monitor, &mi);

		if (!EqualRect(&origMi.rcMonitor, &mi.rcMonitor))
		{
			RECT wr = {};
			GetWindowRect(hwnd, &wr);
			const LONG width = wr.right - wr.left;
			const LONG height = wr.bottom - wr.top;

			const RECT& mr = 0 == g_inMessageBox ? mi.rcWork : mi.rcMonitor;
			const LONG left = (mr.left + mr.right - width) / 2;
			const LONG top = (mr.top + mr.bottom - height) / 2;

			CALL_ORIG_FUNC(SetWindowPos)(hwnd, nullptr, left, top, width, height,
				SWP_NOACTIVATE | SWP_NOZORDER | SWP_NOSENDCHANGING);
		}
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

	void onDestroyWindow(HWND hwnd)
	{
		if (Gdi::Window::isTopLevelWindow(hwnd))
		{
			Gdi::Window::updateAll();
			Gdi::GuiThread::deleteTaskbarTab(hwnd);
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

		if (Gdi::Window::isTopLevelWindow(hwnd))
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
		if (Gdi::Window::isTopLevelWindow(hwnd))
		{
			wp.flags |= SWP_NOREDRAW;
		}
		else
		{
			wp.flags |= SWP_NOCOPYBITS;
		}
	}

	BOOL peekMessage(LPMSG lpMsg, HWND hWnd, UINT wMsgFilterMin, UINT wMsgFilterMax, UINT wRemoveMsg,
		decltype(&PeekMessageA) origPeekMessage)
	{
		DDraw::RealPrimarySurface::setUpdateReady();
		BOOL result = origPeekMessage(lpMsg, hWnd, wMsgFilterMin, wMsgFilterMax, wRemoveMsg);
		if (!g_isFrameStarted || Config::Settings::FpsLimiter::MSGLOOP != Config::fpsLimiter.get())
		{
			return result;
		}

		auto qpcNow = Time::queryPerformanceCounter();
		if (qpcNow - g_qpcWaitEnd >= 0)
		{
			if (!g_waiting)
			{
				g_qpcWaitEnd = qpcNow;
			}
			g_isFrameStarted = false;
			g_waiting = false;
			return result;
		}

		g_waiting = true;
		if (result)
		{
			return result;
		}

		Compat::ScopedThreadPriority prio(THREAD_PRIORITY_TIME_CRITICAL);
		while (Time::qpcToMs(g_qpcWaitEnd - qpcNow) > 0)
		{
			Time::waitForNextTick();
			if (origPeekMessage(lpMsg, hWnd, wMsgFilterMin, wMsgFilterMax, wRemoveMsg))
			{
				return TRUE;
			}
			qpcNow = Time::queryPerformanceCounter();
		}

		while (g_qpcWaitEnd - qpcNow > 0)
		{
			if (origPeekMessage(lpMsg, hWnd, wMsgFilterMin, wMsgFilterMax, wRemoveMsg))
			{
				return TRUE;
			}
			qpcNow = Time::queryPerformanceCounter();
		}

		g_isFrameStarted = false;
		g_waiting = false;
		return result;
	}

	BOOL WINAPI peekMessageA(LPMSG lpMsg, HWND hWnd, UINT wMsgFilterMin, UINT wMsgFilterMax, UINT wRemoveMsg)
	{
		return peekMessage(lpMsg, hWnd, wMsgFilterMin, wMsgFilterMax, wRemoveMsg, CALL_ORIG_FUNC(PeekMessageA));
	}

	BOOL WINAPI peekMessageW(LPMSG lpMsg, HWND hWnd, UINT wMsgFilterMin, UINT wMsgFilterMax, UINT wRemoveMsg)
	{
		return peekMessage(lpMsg, hWnd, wMsgFilterMin, wMsgFilterMax, wRemoveMsg, CALL_ORIG_FUNC(PeekMessageW));
	}

	BOOL WINAPI setLayeredWindowAttributes(HWND hwnd, COLORREF crKey, BYTE bAlpha, DWORD dwFlags)
	{
		LOG_FUNC("SetLayeredWindowAttributes", hwnd, crKey, bAlpha, dwFlags);
		BOOL result = CALL_ORIG_FUNC(SetLayeredWindowAttributes)(hwnd, crKey, bAlpha, dwFlags);
		if (result && DDraw::RealPrimarySurface::isFullscreen())
		{
			DDraw::RealPrimarySurface::scheduleOverlayUpdate();
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
		else if ((GWL_STYLE == nIndex || GWL_EXSTYLE == nIndex) && Config::removeBorders.get())
		{
			auto tagSurface = DDraw::TagSurface::findFullscreenWindow(hWnd);
			if (tagSurface)
			{
				if (GWL_STYLE == nIndex)
				{
					return tagSurface->setWindowStyle(dwNewLong);
				}
				else
				{
					return tagSurface->setWindowExStyle(dwNewLong);
				}
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
			if (OBJID_WINDOW == idObject)
			{
				Gdi::WinProc::onCreateWindow(hwnd);
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
			HOOK_FUNCTION(user32, CreateDialogIndirectParamA, createDialog<CreateDialogIndirectParamA>);
			HOOK_FUNCTION(user32, CreateDialogIndirectParamW, createDialog<CreateDialogIndirectParamW>);
			HOOK_FUNCTION(user32, CreateDialogParamA, createDialog<CreateDialogParamA>);
			HOOK_FUNCTION(user32, CreateDialogParamW, createDialog<CreateDialogParamW>);
			HOOK_FUNCTION(user32, DialogBoxParamA, createDialog<DialogBoxParamA>);
			HOOK_FUNCTION(user32, DialogBoxParamW, createDialog<DialogBoxParamW>);
			HOOK_FUNCTION(user32, DialogBoxIndirectParamA, createDialog<DialogBoxIndirectParamA>);
			HOOK_FUNCTION(user32, DialogBoxIndirectParamW, createDialog<DialogBoxIndirectParamW>);
			HOOK_FUNCTION(user32, GetMessageA, getMessageA);
			HOOK_FUNCTION(user32, GetMessageW, getMessageW);
			HOOK_FUNCTION(user32, GetWindowLongA, getWindowLongA);
			HOOK_FUNCTION(user32, GetWindowLongW, getWindowLongW);
			HOOK_FUNCTION(user32, MessageBoxA, messageBox<MessageBoxA>);
			HOOK_FUNCTION(user32, MessageBoxW, messageBox<MessageBoxW>);
			HOOK_FUNCTION(user32, MessageBoxExA, messageBox<MessageBoxExA>);
			HOOK_FUNCTION(user32, MessageBoxExW, messageBox<MessageBoxExW>);
			HOOK_FUNCTION(user32, MessageBoxIndirectA, messageBox<MessageBoxIndirectA>);
			HOOK_FUNCTION(user32, MessageBoxIndirectW, messageBox<MessageBoxIndirectW>);
			HOOK_FUNCTION(user32, PeekMessageA, peekMessageA);
			HOOK_FUNCTION(user32, PeekMessageW, peekMessageW);
			HOOK_FUNCTION(user32, SetLayeredWindowAttributes, setLayeredWindowAttributes);
			HOOK_FUNCTION(user32, SetWindowLongA, setWindowLongA);
			HOOK_FUNCTION(user32, SetWindowLongW, setWindowLongW);
			HOOK_FUNCTION(user32, SetWindowPos, setWindowPos);

			Compat::hookIatFunction(Dll::g_origDDrawModule, "SetWindowLongA", ddrawSetWindowLongA);

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
			LOG_FUNC("onCreateWindow", hwnd);
			if (!GuiThread::isReady() || GuiThread::isGuiThreadWindow(hwnd))
			{
				return;
			}

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

			if (!Gdi::Window::isTopLevelWindow(hwnd))
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
		
		void startFrame()
		{
			if (Config::Settings::FpsLimiter::MSGLOOP != Config::fpsLimiter.get() || g_inWindowProc)
			{
				return;
			}

			auto fps = Config::fpsLimiter.getParam();
			if (0 == fps)
			{
				fps = 1000;
			}

			if (!g_isFrameStarted)
			{
				g_qpcWaitEnd += Time::g_qpcFrequency / fps;
				g_isFrameStarted = true;
				return;
			}

			if (!g_waiting)
			{
				return;
			}

			g_qpcWaitEnd += Time::g_qpcFrequency / fps;
			g_waiting = false;

			auto qpcNow = Time::queryPerformanceCounter();
			if (qpcNow - g_qpcWaitEnd >= 0)
			{
				return;
			}

			Compat::ScopedThreadPriority prio(THREAD_PRIORITY_TIME_CRITICAL);
			while (Time::qpcToMs(g_qpcWaitEnd - qpcNow) > 0)
			{
				Time::waitForNextTick();
				DDraw::RealPrimarySurface::flush();
				qpcNow = Time::queryPerformanceCounter();
			}

			while (g_qpcWaitEnd - qpcNow > 0)
			{
				qpcNow = Time::queryPerformanceCounter();
			}
		}

		void watchWindowPosChanges(WindowPosChangeNotifyFunc notifyFunc)
		{
			g_windowPosChangeNotifyFuncs.insert(notifyFunc);
		}
	}
}
