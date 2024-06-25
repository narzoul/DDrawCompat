#include <map>
#include <set>

#include <Windows.h>
#include <Windowsx.h>
#include <dwmapi.h>

#include <Common/Hook.h>
#include <Common/Log.h>
#include <Common/ScopedSrwLock.h>
#include <Common/ScopedThreadPriority.h>
#include <Common/Time.h>
#include <Config/Settings/FpsLimiter.h>
#include <Config/Settings/RemoveBorders.h>
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
#include <Win32/DpiAwareness.h>

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
		WNDPROC ddrawOrigWndProc;
	};

	decltype(&DwmSetIconicThumbnail) g_dwmSetIconicThumbnail = nullptr;

	std::map<HMENU, UINT> g_menuMaxHeight;

	Compat::SrwLock g_windowProcSrwLock;
	std::map<HWND, WindowProc> g_windowProc;

	thread_local POINT* g_cursorPos = nullptr;
	thread_local unsigned g_inCreateDialog = 0;
	thread_local unsigned g_inMessageBox = 0;
	thread_local unsigned g_inWindowProc = 0;
	thread_local long long g_qpcWaitEnd = 0;
	thread_local bool g_isFrameStarted = false;
	thread_local bool g_waiting = false;

	void dwmSendIconicThumbnail(HWND hwnd, LONG width, LONG height);
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

	BOOL WINAPI animateWindow(HWND hWnd, DWORD dwTime, DWORD dwFlags)
	{
		LOG_FUNC("AnimateWindow", hWnd, dwTime, Compat::hex(dwFlags));
		if (dwFlags & AW_BLEND)
		{
			dwFlags &= ~AW_BLEND;
			dwFlags |= AW_SLIDE | AW_HOR_POSITIVE;
		}
		return LOG_RESULT(CALL_ORIG_FUNC(AnimateWindow)(hWnd, dwTime, dwFlags));
	}

	void clipMouseCoords(MSG& msg)
	{
		Gdi::Cursor::clip(msg.pt);

		bool isClient = false;
		if (msg.message >= WM_MOUSEFIRST && msg.message <= WM_MOUSELAST)
		{
			isClient = WM_MOUSEWHEEL != msg.message && WM_MOUSEHWHEEL != msg.message;
		}
		else if (msg.message >= WM_NCMOUSEMOVE && msg.message <= WM_NCMBUTTONDBLCLK ||
			msg.message >= WM_NCXBUTTONDOWN && msg.message <= WM_NCXBUTTONDBLCLK ||
			WM_NCHITTEST == msg.message)
		{
			isClient = false;
		}
		else if (WM_MOUSEHOVER == msg.message ||
			WM_NCMOUSEHOVER == msg.message)
		{
			isClient = true;
		}
		else
		{
			return;
		}

		POINT pt = {};
		pt.x = GET_X_LPARAM(msg.lParam);
		pt.y = GET_Y_LPARAM(msg.lParam);

		if (isClient)
		{
			ClientToScreen(msg.hwnd, &pt);
		}

		Gdi::Cursor::clip(pt);

		if (isClient)
		{
			ScreenToClient(msg.hwnd, &pt);
		}

		reinterpret_cast<POINTS*>(&msg.lParam)->x = static_cast<SHORT>(pt.x);
		reinterpret_cast<POINTS*>(&msg.lParam)->y = static_cast<SHORT>(pt.y);
	}

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
		POINT cursorPos = {};

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

		case WM_DWMSENDICONICTHUMBNAIL:
			dwmSendIconicThumbnail(hwnd, HIWORD(lParam), LOWORD(lParam));
			return 0;

		case WM_GETMINMAXINFO:
			onGetMinMaxInfo(*reinterpret_cast<MINMAXINFO*>(lParam));
			break;

		case WM_INITDIALOG:
			onInitDialog(hwnd);
			break;

		case WM_MOUSEMOVE:
			if (1 == g_inWindowProc)
			{
				cursorPos = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
				ClientToScreen(hwnd, &cursorPos);
				g_cursorPos = &cursorPos;
			}
			break;

		case WM_SYNCPAINT:
			if (Gdi::Window::isTopLevelWindow(hwnd))
			{
				RECT emptyRect = {};
				RedrawWindow(hwnd, &emptyRect, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ERASENOW);
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

		case WM_MOUSEMOVE:
			if (1 == g_inWindowProc)
			{
				g_cursorPos = nullptr;
			}
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
			auto origWndProc = setWindowLongA(hWnd, GWL_WNDPROC, dwNewLong);
			if (Dll::g_origDDrawModule == Compat::getModuleHandleFromAddress(reinterpret_cast<void*>(dwNewLong)))
			{
				Compat::ScopedSrwLockExclusive lock(g_windowProcSrwLock);
				auto it = g_windowProc.find(hWnd);
				if (it != g_windowProc.end())
				{
					it->second.ddrawOrigWndProc = reinterpret_cast<WNDPROC>(origWndProc);
				}
				DDraw::DirectDraw::hookDDrawWindowProc(reinterpret_cast<WNDPROC>(dwNewLong));
			}
			return origWndProc;
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

	void dwmSendIconicThumbnail(HWND hwnd, LONG width, LONG height)
	{
		auto presentationWindow = Gdi::Window::getPresentationWindow(hwnd);
		if (!presentationWindow || !g_dwmSetIconicThumbnail)
		{
			return;
		}

		BITMAPINFO bmi = {};
		bmi.bmiHeader.biSize = sizeof(bmi.bmiHeader);
		bmi.bmiHeader.biWidth = width;
		bmi.bmiHeader.biHeight = -height;
		bmi.bmiHeader.biPlanes = 1;
		bmi.bmiHeader.biBitCount = 32;
		bmi.bmiHeader.biCompression = BI_RGB;
		void* bits = nullptr;
		HBITMAP bmp = CreateDIBSection(nullptr, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
		if (!bmp)
		{
			return;
		}

		HDC srcDc = GetWindowDC(presentationWindow);
		HDC dstDc = CreateCompatibleDC(nullptr);
		auto prevBmp = SelectBitmap(dstDc, bmp);

		RECT srcRect = {};
		GetWindowRect(presentationWindow, &srcRect);

		SetStretchBltMode(dstDc, HALFTONE);
		CALL_ORIG_FUNC(StretchBlt)(dstDc, 0, 0, width, height,
			srcDc, 0, 0, srcRect.right - srcRect.left, srcRect.bottom - srcRect.top, SRCCOPY);

		SelectBitmap(dstDc, prevBmp);
		DeleteDC(dstDc);
		ReleaseDC(presentationWindow, srcDc);

		Win32::ScopedDpiAwareness dpiAwareness;
		g_dwmSetIconicThumbnail(hwnd, bmp, 0);
		DeleteObject(bmp);
	}

	BOOL WINAPI getCursorPos(LPPOINT lpPoint)
	{
		if (lpPoint && g_cursorPos)
		{
			*lpPoint = *g_cursorPos;
			return TRUE;
		}

		BOOL result = CALL_ORIG_FUNC(GetCursorPos)(lpPoint);
		if (result)
		{
			Gdi::Cursor::clip(*lpPoint);
		}
		return result;
	}

	BOOL WINAPI getMessage(LPMSG lpMsg, HWND hWnd, UINT wMsgFilterMin, UINT wMsgFilterMax,
		decltype(&GetMessageA) origGetMessage)
	{
		DDraw::RealPrimarySurface::setUpdateReady();
		BOOL result = origGetMessage(lpMsg, hWnd, wMsgFilterMin, wMsgFilterMax);
		if (-1 != result)
		{
			clipMouseCoords(*lpMsg);
		}
		return result;
	}

	BOOL WINAPI getMessageA(LPMSG lpMsg, HWND hWnd, UINT wMsgFilterMin, UINT wMsgFilterMax)
	{
		return getMessage(lpMsg, hWnd, wMsgFilterMin, wMsgFilterMax, CALL_ORIG_FUNC(GetMessageA));
	}

	BOOL WINAPI getMessageW(LPMSG lpMsg, HWND hWnd, UINT wMsgFilterMin, UINT wMsgFilterMax)
	{
		return getMessage(lpMsg, hWnd, wMsgFilterMin, wMsgFilterMax, CALL_ORIG_FUNC(GetMessageW));
	}

	int WINAPI getRandomRgn(HDC hdc, HRGN hrgn, INT i)
	{
		LOG_FUNC("GetRandomRgn", hdc, hrgn, i);
		return LOG_RESULT(Gdi::Window::getRandomRgn(hdc, hrgn, i));
	}

	LONG getWindowLong(HWND hWnd, int nIndex,
		decltype(&GetWindowLongA) origGetWindowLong, WNDPROC(WindowProc::* wndProc))
	{
		if (GWL_WNDPROC == nIndex)
		{
			Compat::ScopedSrwLockShared lock(g_windowProcSrwLock);
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
		Compat::ScopedSrwLockShared lock(g_windowProcSrwLock);
		auto it = g_windowProc.find(hwnd);
		return it != g_windowProc.end() ? it->second : WindowProc{};
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

		auto mi = Win32::DisplayMode::getMonitorInfo(hwnd);

		if (!EqualRect(&mi.rcEmulated, &mi.rcMonitor))
		{
			RECT wr = {};
			GetWindowRect(hwnd, &wr);
			const LONG width = wr.right - wr.left;
			const LONG height = wr.bottom - wr.top;

			if (0 == g_inMessageBox)
			{
				mi.rcWork.right = mi.rcWork.left + mi.rcEmulated.right - mi.rcEmulated.left;
				mi.rcWork.bottom = mi.rcWork.top + mi.rcEmulated.bottom - mi.rcEmulated.top;
			}

			const RECT& mr = 0 == g_inMessageBox ? mi.rcWork : mi.rcEmulated;
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
			Gdi::Window::destroyWindow(hwnd);
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
		const auto& mi = Win32::DisplayMode::getMonitorInfo();
		mmi.ptMaxSize.x = mi.rcEmulated.right - 2 * mmi.ptMaxPosition.x;
		mmi.ptMaxSize.y = mi.rcEmulated.bottom - 2 * mmi.ptMaxPosition.y;
	}

	void onInitMenuPopup(HMENU menu)
	{
		auto deviceName = Win32::DisplayMode::getEmulatedDisplayMode().deviceName;
		if (deviceName.empty())
		{
			return;
		}
		const RECT& mr = Win32::DisplayMode::getMonitorInfo(deviceName).rcEmulated;

		MENUINFO mi = {};
		mi.cbSize = sizeof(mi);
		mi.fMask = MIM_MAXHEIGHT;
		GetMenuInfo(menu, &mi);

		UINT maxHeight = mr.bottom - mr.top;
		if (0 == mi.cyMax || mi.cyMax > maxHeight)
		{
			g_menuMaxHeight[menu] = mi.cyMax;
			mi.cyMax = maxHeight;
			SetMenuInfo(menu, &mi);
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
		if (Gdi::Window::isTopLevelWindow(hwnd))
		{
			DDraw::RealPrimarySurface::setPresentationWindowTopmost();
			Gdi::Window::updateWindowPos(hwnd);

			if (g_dwmSetIconicThumbnail)
			{
				const BOOL isIconic = IsIconic(hwnd);
				DwmSetWindowAttribute(hwnd, DWMWA_FORCE_ICONIC_REPRESENTATION, &isIconic, sizeof(isIconic));
				DwmSetWindowAttribute(hwnd, DWMWA_HAS_ICONIC_BITMAP, &isIconic, sizeof(isIconic));
			}
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
		if (result)
		{
			clipMouseCoords(*lpMsg);
		}

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

		if ((uFlags & SWP_NOACTIVATE) && !(uFlags && SWP_NOZORDER) &&
			(HWND_TOP == hWndInsertAfter || HWND_TOPMOST == hWndInsertAfter) &&
			(GetWindowLong(hWnd, GWL_EXSTYLE) & WS_EX_TOPMOST))
		{
			const HWND topmost = DDraw::RealPrimarySurface::getTopmost();
			if (topmost != HWND_TOPMOST)
			{
				hWndInsertAfter = topmost;
			}
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

	BOOL WINAPI updateLayeredWindow(HWND hWnd, HDC hdcDst, POINT* pptDst, SIZE* psize,
		HDC hdcSrc, POINT* pptSrc, COLORREF crKey, BLENDFUNCTION* pblend, DWORD dwFlags)
	{
		LOG_FUNC("UpdateLayeredWindow", hWnd, hdcDst, pptDst, psize, hdcSrc, pptSrc, crKey, pblend, dwFlags);
		BOOL result = CALL_ORIG_FUNC(UpdateLayeredWindow)(
			hWnd, hdcDst, pptDst, psize, hdcSrc, pptSrc, crKey, pblend, dwFlags);
		if (result)
		{
			Gdi::Window::updateLayeredWindowInfo(hWnd, hdcSrc, pptSrc,
				(dwFlags & ULW_COLORKEY) ? crKey : CLR_INVALID,
				((dwFlags & ULW_ALPHA) && pblend) ? pblend->SourceConstantAlpha : 255,
				pblend ? pblend->AlphaFormat : 0);
		}
		return LOG_RESULT(result);
	}

	BOOL WINAPI updateLayeredWindowIndirect(HWND hwnd, const UPDATELAYEREDWINDOWINFO* pULWInfo)
	{
		LOG_FUNC("UpdateLayeredWindowIndirect", hwnd, pULWInfo);
		BOOL result = CALL_ORIG_FUNC(UpdateLayeredWindowIndirect)(hwnd, pULWInfo);
		if (result && pULWInfo)
		{
			Gdi::Window::updateLayeredWindowInfo(hwnd, pULWInfo->hdcSrc, pULWInfo->pptSrc,
				(pULWInfo->dwFlags & ULW_COLORKEY) ? pULWInfo->crKey : CLR_INVALID,
				((pULWInfo->dwFlags & ULW_ALPHA) && pULWInfo->pblend) ? pULWInfo->pblend->SourceConstantAlpha : 255,
				pULWInfo->pblend ? pULWInfo->pblend->AlphaFormat : 0);
		}
		return LOG_RESULT(result);
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
			if (OBJID_WINDOW == idObject && Gdi::Window::isTopLevelWindow(hwnd) && !Gdi::GuiThread::isGuiThreadWindow(hwnd))
			{
				auto presentationWindow = Gdi::Window::getPresentationWindow(hwnd);
				if (presentationWindow)
				{
					Gdi::Window::updatePresentationWindowText(presentationWindow);
				}
				break;
			}

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
			break;
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

		WNDPROC getDDrawOrigWndProc(HWND hwnd)
		{
			Compat::ScopedSrwLockShared lock(g_windowProcSrwLock);
			auto it = g_windowProc.find(hwnd);
			return it != g_windowProc.end() ? it->second.ddrawOrigWndProc : nullptr;
		}

		void installHooks()
		{
			HOOK_FUNCTION(user32, AnimateWindow, animateWindow);
			HOOK_FUNCTION(user32, CreateDialogIndirectParamA, createDialog<CreateDialogIndirectParamA>);
			HOOK_FUNCTION(user32, CreateDialogIndirectParamW, createDialog<CreateDialogIndirectParamW>);
			HOOK_FUNCTION(user32, CreateDialogParamA, createDialog<CreateDialogParamA>);
			HOOK_FUNCTION(user32, CreateDialogParamW, createDialog<CreateDialogParamW>);
			HOOK_FUNCTION(user32, DialogBoxParamA, createDialog<DialogBoxParamA>);
			HOOK_FUNCTION(user32, DialogBoxParamW, createDialog<DialogBoxParamW>);
			HOOK_FUNCTION(user32, DialogBoxIndirectParamA, createDialog<DialogBoxIndirectParamA>);
			HOOK_FUNCTION(user32, DialogBoxIndirectParamW, createDialog<DialogBoxIndirectParamW>);
			HOOK_FUNCTION(user32, GetCursorPos, getCursorPos);
			HOOK_FUNCTION(user32, GetMessageA, getMessageA);
			HOOK_FUNCTION(user32, GetMessageW, getMessageW);
			HOOK_FUNCTION(gdi32, GetRandomRgn, getRandomRgn);
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
			HOOK_FUNCTION(user32, UpdateLayeredWindow, updateLayeredWindow);
			HOOK_FUNCTION(user32, UpdateLayeredWindowIndirect, updateLayeredWindowIndirect);

			g_dwmSetIconicThumbnail = reinterpret_cast<decltype(&DwmSetIconicThumbnail)>(
				GetProcAddress(GetModuleHandle("dwmapi"), "DwmSetIconicThumbnail"));

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

			DWMNCRENDERINGPOLICY ncRenderingPolicy = DWMNCRP_DISABLED;
			DwmSetWindowAttribute(hwnd, DWMWA_NCRENDERING_POLICY, &ncRenderingPolicy, sizeof(ncRenderingPolicy));

			BOOL disableTransitions = TRUE;
			DwmSetWindowAttribute(hwnd, DWMWA_TRANSITIONS_FORCEDISABLED, &disableTransitions, sizeof(disableTransitions));

			const auto style = GetClassLong(hwnd, GCL_STYLE);
			if (style & CS_DROPSHADOW)
			{
				CALL_ORIG_FUNC(SetClassLongA)(hwnd, GCL_STYLE, style & ~CS_DROPSHADOW);
			}

			Gdi::Window::updateWindowPos(hwnd);
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
	}
}
