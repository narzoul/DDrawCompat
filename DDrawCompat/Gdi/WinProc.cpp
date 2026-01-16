#include <map>

#include <Windows.h>
#include <Windowsx.h>
#include <dwmapi.h>

#include <Common/Hook.h>
#include <Common/Log.h>
#include <Common/ScopedSrwLock.h>
#include <Common/ScopedThreadPriority.h>
#include <Common/Time.h>
#include <Config/Settings/CompatFixes.h>
#include <Config/Settings/FpsLimiter.h>
#include <Dll/Dll.h>
#include <DDraw/DirectDraw.h>
#include <DDraw/RealPrimarySurface.h>
#include <DDraw/Surfaces/TagSurface.h>
#include <Gdi/Cursor.h>
#include <Gdi/Gdi.h>
#include <Gdi/GuiThread.h>
#include <Gdi/ScrollBar.h>
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
	};

	decltype(&DwmSetIconicThumbnail) g_dwmSetIconicThumbnail = nullptr;
	decltype(&GetDpiForSystem) g_getDpiForSystem = nullptr;

	std::map<HMENU, UINT> g_menuMaxHeight;

	Compat::SrwLock g_windowProcSrwLock;
	std::map<HWND, WindowProc> g_windowProc;
	std::map<HWND, WNDPROC> g_ddrawWindowProc;

	thread_local POINT* g_cursorPos = nullptr;
	thread_local HWND g_droppedDownComboBox = nullptr;
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

		if (g_droppedDownComboBox && Gdi::getComboLBoxAtom() == GetClassLongA(hWnd, GCW_ATOM))
		{
			if ((dwFlags & AW_VER_POSITIVE))
			{
				dwFlags = Gdi::WinProc::adjustComboListBoxRect(hWnd, dwFlags);
			}
			if (CALL_ORIG_FUNC(AnimateWindow)(hWnd, dwTime, dwFlags))
			{
				return LOG_RESULT(TRUE);
			}
			RedrawWindow(hWnd, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN | RDW_ERASENOW);
			return LOG_RESULT(FALSE);
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
		case WM_COMMAND:
			if (CBN_DROPDOWN == HIWORD(wParam))
			{
				g_droppedDownComboBox = reinterpret_cast<HWND>(lParam);
			}
			else if (CBN_CLOSEUP == HIWORD(wParam))
			{
				g_droppedDownComboBox = nullptr;
			}
			break;

		case WM_DISPLAYCHANGE:
			if (0 != wParam)
			{
				return LOG_RESULT(0);
			}
			wParam = Win32::DisplayMode::getBpp();
			break;

		case WM_DWMCOMPOSITIONCHANGED:
			Gdi::checkDesktopComposition();
			break;

		case WM_DWMSENDICONICTHUMBNAIL:
			dwmSendIconicThumbnail(hwnd, HIWORD(lParam), LOWORD(lParam));
			return LOG_RESULT(0);

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
				return LOG_RESULT(0);
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
					static WPARAM isActive = TRUE;
					if (wParam == isActive)
					{
						return;
					}
					isActive = wParam;

					static bool configVisible = false;
					static bool statsVisible = false;

					auto configWindow = Gdi::GuiThread::getConfigWindow();
					auto statsWindow = Gdi::GuiThread::getStatsWindow();
					if (!wParam)
					{
						configVisible = configWindow ? configWindow->isVisible() : false;
						statsVisible = statsWindow ? statsWindow->isVisible() : false;
					}

					if (configWindow)
					{
						configWindow->setVisible(wParam ? configVisible : false);
					}
					if (statsWindow)
					{
						statsWindow->setVisible(wParam ? statsVisible : false);
					}

					if (!wParam)
					{
						CALL_ORIG_FUNC(ClipCursor)(nullptr);
					}
				});
			break;

		case WM_CTLCOLORSCROLLBAR:
			if (reinterpret_cast<HWND>(lParam) != hwnd &&
				isUser32ScrollBar(reinterpret_cast<HWND>(lParam)))
			{
				if (Gdi::isRedirected(hwnd))
				{
					Gdi::ScrollBar::onCtlColorScrollBar(hwnd, wParam, lParam, result);
				}
				else
				{
					DDraw::RealPrimarySurface::scheduleOverlayUpdate();
				}
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
			Gdi::Cursor::setCursor();
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
				g_ddrawWindowProc[hWnd] = reinterpret_cast<WNDPROC>(origWndProc);
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
		CALL_ORIG_FUNC(GetWindowRect)(presentationWindow, &srcRect);

		SetStretchBltMode(dstDc, HALFTONE);
		CALL_ORIG_FUNC(StretchBlt)(dstDc, 0, 0, width, height,
			srcDc, 0, 0, srcRect.right - srcRect.left, srcRect.bottom - srcRect.top, SRCCOPY);

		SelectBitmap(dstDc, prevBmp);
		DeleteDC(dstDc);
		ReleaseDC(presentationWindow, srcDc);

		Win32::ScopedDpiAwareness dpiAwareness;
		g_dwmSetIconicThumbnail(hwnd, bmp, 0);
		CALL_ORIG_FUNC(DeleteObject)(bmp);
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

	UINT WINAPI getDpiForSystem()
	{
		LOG_FUNC("GetDpiForSystem");
		if (0 != g_inCreateDialog || 0 != g_inMessageBox)
		{
			return LOG_RESULT(96);
		}
		return LOG_RESULT(g_getDpiForSystem());
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

	BOOL WINAPI getWindowRect(HWND hWnd, LPRECT lpRect)
	{
		BOOL result = CALL_ORIG_FUNC(GetWindowRect)(hWnd, lpRect);
		if (result && hWnd && CALL_ORIG_FUNC(GetDesktopWindow)() == hWnd)
		{
			const auto& dm = Win32::DisplayMode::getEmulatedDisplayMode();
			if (0 != dm.width)
			{
				lpRect->right = lpRect->left + dm.width;
				lpRect->bottom = lpRect->top + dm.height;
			}
		}
		return result;
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
			CALL_ORIG_FUNC(GetWindowRect)(hwnd, &wr);
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
		static const ATOM sbAtom = Gdi::getClassAtom(L"ScrollBar");
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

			if (g_dwmSetIconicThumbnail && Gdi::isRedirected(hwnd))
			{
				const BOOL isIconic = IsIconic(hwnd);
				DwmSetWindowAttribute(hwnd, DWMWA_FORCE_ICONIC_REPRESENTATION, &isIconic, sizeof(isIconic));
				DwmSetWindowAttribute(hwnd, DWMWA_HAS_ICONIC_BITMAP, &isIconic, sizeof(isIconic));
			}
		}

		if (wp.flags & SWP_FRAMECHANGED)
		{
			if (Gdi::isRedirected(hwnd))
			{
				RECT r = { -1, -1, 0, 0 };
				RedrawWindow(hwnd, &r, nullptr, RDW_INVALIDATE | RDW_FRAME);
			}
			else
			{
				DDraw::RealPrimarySurface::scheduleOverlayUpdate();
			}
		}
	}

	void onWindowPosChanging(HWND hwnd, WINDOWPOS& wp)
	{
		if (Gdi::isRedirected(hwnd))
		{
			wp.flags |= Gdi::Window::isTopLevelWindow(hwnd) ? SWP_NOREDRAW : SWP_NOCOPYBITS;
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

		if (!g_isFrameStarted)
		{
			return result;
		}

		const auto fpsLimiter = DDraw::RealPrimarySurface::getFpsLimiter();
		if (Config::Settings::FpsLimiter::MSGLOOP != fpsLimiter.value)
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
		else if ((GWL_STYLE == nIndex || GWL_EXSTYLE == nIndex) && Config::compatFixes.get().nowindowborders)
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

		if ((SWP_FRAMECHANGED | SWP_NOACTIVATE | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_SHOWWINDOW) == uFlags &&
			!IsWindowVisible(hWnd))
		{
			char name[32] = {};
			GetClassNameA(hWnd, name, sizeof(name));
			if (0 == strcmp(name, "VideoRenderer"))
			{
				uFlags &= ~SWP_SHOWWINDOW;
			}
		}

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

	int WINAPI setWindowRgn(HWND hWnd, HRGN hRgn, BOOL bRedraw)
	{
		LOG_FUNC("SetWindowRgn", hWnd, hRgn, bRedraw);
		int result = CALL_ORIG_FUNC(SetWindowRgn)(hWnd, hRgn, bRedraw);
		if (result && Gdi::Window::isTopLevelWindow(hWnd))
		{
			Gdi::Window::updateWindowPos(hWnd);
		}
		return LOG_RESULT(result);
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
				if (Gdi::isRedirected(hwnd))
				{
					HDC dc = GetWindowDC(hwnd);
					Gdi::TitleBar(hwnd).drawButtons(dc);
					ReleaseDC(hwnd, dc);
				}
				else
				{
					DDraw::RealPrimarySurface::scheduleOverlayUpdate();
				}
				break;

			case OBJID_CLIENT:
				if (!isUser32ScrollBar(hwnd))
				{
					break;
				}
			case OBJID_HSCROLL:
			case OBJID_VSCROLL:
				if (Gdi::isRedirected(hwnd))
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
				}
				else
				{
					DDraw::RealPrimarySurface::scheduleOverlayUpdate();
				}
				break;
			}
			break;
		}
	}
}

namespace Gdi
{
	namespace WinProc
	{
		DWORD adjustComboListBoxRect(HWND hwnd, DWORD awFlags)
		{
			LOG_FUNC("adjustComboListBoxRect", hwnd, awFlags, g_droppedDownComboBox);
			if (!g_droppedDownComboBox)
			{
				return LOG_RESULT(awFlags);
			}

			SetPropA(hwnd, "DDCRedirected", reinterpret_cast<HANDLE>(isRedirected(g_droppedDownComboBox)));

			MONITORINFO mi = {};
			mi.cbSize = sizeof(mi);
			GetMonitorInfoA(CALL_ORIG_FUNC(MonitorFromWindow)(hwnd, MONITOR_DEFAULTTOPRIMARY), &mi);

			RECT cbRect = {};
			CALL_ORIG_FUNC(GetWindowRect)(g_droppedDownComboBox, &cbRect);
			RECT lbRect = {};
			CALL_ORIG_FUNC(GetWindowRect)(hwnd, &lbRect);

			if (lbRect.bottom > mi.rcMonitor.bottom)
			{
				if (cbRect.top - mi.rcMonitor.top > mi.rcMonitor.bottom - cbRect.bottom)
				{
					const LONG lbHeight = lbRect.bottom - lbRect.top;
					lbRect.bottom = cbRect.top;
					lbRect.top = std::max(cbRect.top - lbHeight, mi.rcMonitor.top);
					awFlags &= ~AW_VER_POSITIVE;
					awFlags |= AW_VER_NEGATIVE;
				}
				else
				{
					lbRect.bottom = mi.rcMonitor.bottom;
				}

				CALL_ORIG_FUNC(SetWindowPos)(hwnd, nullptr, lbRect.left, lbRect.top,
					lbRect.right - lbRect.left, lbRect.bottom - lbRect.top, SWP_NOZORDER | SWP_NOACTIVATE);

				if (awFlags & AW_VER_NEGATIVE)
				{
					CALL_ORIG_FUNC(GetWindowRect)(hwnd, &lbRect);
					if (lbRect.bottom != cbRect.top)
					{
						OffsetRect(&lbRect, 0, cbRect.top - lbRect.bottom);
						CALL_ORIG_FUNC(SetWindowPos)(hwnd, nullptr, lbRect.left, lbRect.top,
							lbRect.right - lbRect.left, lbRect.bottom - lbRect.top, SWP_NOZORDER | SWP_NOACTIVATE);
					}
				}
			}
			return LOG_RESULT(awFlags);
		}

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
			auto it = g_ddrawWindowProc.find(hwnd);
			return it != g_ddrawWindowProc.end() ? it->second : nullptr;
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
			HOOK_FUNCTION(user32, GetWindowRect, getWindowRect);
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
			HOOK_FUNCTION(user32, SetWindowRgn, setWindowRgn);
			HOOK_FUNCTION(user32, UpdateLayeredWindow, updateLayeredWindow);
			HOOK_FUNCTION(user32, UpdateLayeredWindowIndirect, updateLayeredWindowIndirect);

			g_dwmSetIconicThumbnail = GET_PROC_ADDRESS(dwmapi, DwmSetIconicThumbnail);
			g_getDpiForSystem = GET_PROC_ADDRESS(user32, GetDpiForSystem);
			if (g_getDpiForSystem)
			{
				Compat::hookFunction(reinterpret_cast<void*&>(g_getDpiForSystem), getDpiForSystem, "GetDpiForSystem");
			}

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

			BOOL disableTransitions = TRUE;
			DwmSetWindowAttribute(hwnd, DWMWA_TRANSITIONS_FORCEDISABLED, &disableTransitions, sizeof(disableTransitions));

			Gdi::Window::updateWindowPos(hwnd);
		}
		
		void startFrame()
		{
			const auto fpsLimiter = DDraw::RealPrimarySurface::getFpsLimiter();
			if (Config::Settings::FpsLimiter::MSGLOOP != fpsLimiter.value || g_inWindowProc)
			{
				return;
			}

			auto fps = fpsLimiter.param;
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
