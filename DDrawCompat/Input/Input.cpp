#include <algorithm>
#include <map>
#include <tuple>

#include <Windows.h>
#include <hidusage.h>

#include <Common/Hook.h>
#include <Common/Log.h>
#include <Config/Settings/TerminateHotKey.h>
#include <Dll/Dll.h>
#include <DDraw/RealPrimarySurface.h>
#include <Gdi/GuiThread.h>
#include <Gdi/PresentationWindow.h>
#include <Input/Input.h>
#include <Overlay/ConfigWindow.h>
#include <Overlay/Window.h>

namespace
{
	struct HotKeyData
	{
		std::function<void(void*)> action;
		void* context;
		bool onKeyDown;
	};

	HANDLE g_bmpArrow = nullptr;
	SIZE g_bmpArrowSize = {};
	Overlay::Control* g_capture = nullptr;
	POINT g_cursorPos = {};
	HWND g_cursorWindow = nullptr;
	std::map<Input::HotKey, HotKeyData> g_hotKeys;
	RECT g_monitorRect = {};
	HHOOK g_keyboardHook = nullptr;
	HHOOK g_mouseHook = nullptr;

	LRESULT CALLBACK lowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam);
	LRESULT CALLBACK lowLevelMouseProc(int nCode, WPARAM wParam, LPARAM lParam);

	LRESULT CALLBACK cursorWindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
	{
		LOG_FUNC("cursorWindowProc", Compat::WindowMessageStruct(hwnd, uMsg, wParam, lParam));
		switch (uMsg)
		{
		case WM_PAINT:
		{
			PAINTSTRUCT ps = {};
			BeginPaint(hwnd, &ps);
			HDC dc = CreateCompatibleDC(nullptr);
			HGDIOBJ origBmp = SelectObject(dc, g_bmpArrow);
			RECT wr = {};
			GetWindowRect(hwnd, &wr);
			CALL_ORIG_FUNC(StretchBlt)(ps.hdc, 0, 0, wr.right - wr.left, wr.bottom - wr.top,
				dc, 0, 0, g_bmpArrowSize.cx, g_bmpArrowSize.cy, SRCCOPY);
			SelectObject(dc, origBmp);
			DeleteDC(dc);
			EndPaint(hwnd, &ps);
			return 0;
		}

		case WM_WINDOWPOSCHANGED:
			DDraw::RealPrimarySurface::scheduleOverlayUpdate();
			break;
		}

		return CALL_ORIG_FUNC(DefWindowProcA)(hwnd, uMsg, wParam, lParam);
	}

	LRESULT CALLBACK lowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam)
	{
		if (HC_ACTION == nCode &&
			(WM_KEYDOWN == wParam || WM_KEYUP == wParam || WM_SYSKEYDOWN == wParam || WM_SYSKEYUP == wParam))
		{
			DWORD pid = 0;
			GetWindowThreadProcessId(GetForegroundWindow(), &pid);
			if (GetCurrentProcessId() == pid)
			{
				auto llHook = reinterpret_cast<const KBDLLHOOKSTRUCT*>(lParam);
				for (auto& hotkey : g_hotKeys)
				{
					if (hotkey.first.vk == llHook->vkCode && Input::areModifierKeysDown(hotkey.first.modifiers))
					{
						if (hotkey.second.onKeyDown == (WM_KEYDOWN == wParam || WM_SYSKEYDOWN == wParam))
						{
							hotkey.second.action(hotkey.second.context);
						}
						return 1;
					}
				}
			}
		}
		return CallNextHookEx(nullptr, nCode, wParam, lParam);
	}

	LRESULT CALLBACK lowLevelMouseProc(int nCode, WPARAM wParam, LPARAM lParam)
	{
		if (HC_ACTION == nCode)
		{
			auto& llHook = *reinterpret_cast<MSLLHOOKSTRUCT*>(lParam);

			if (WM_MOUSEMOVE == wParam)
			{
				POINT cp = g_cursorPos;
				POINT origCp = {};
				GetCursorPos(&origCp);

				cp.x += (llHook.pt.x - origCp.x);
				cp.y += (llHook.pt.y - origCp.y);
				cp.x = std::min(std::max(g_monitorRect.left, cp.x), g_monitorRect.right);
				cp.y = std::min(std::max(g_monitorRect.top, cp.y), g_monitorRect.bottom);
				g_cursorPos = cp;
				DDraw::RealPrimarySurface::scheduleOverlayUpdate();
			}

			auto cp = Input::getRelativeCursorPos();

			switch (wParam)
			{
			case WM_LBUTTONDOWN:
				g_capture->onLButtonDown(cp);
				break;

			case WM_LBUTTONUP:
				g_capture->onLButtonUp(cp);
				break;

			case WM_MOUSEMOVE:
				g_capture->onMouseMove(cp);
				break;

			case WM_MOUSEWHEEL:
				g_capture->onMouseWheel(cp, HIWORD(llHook.mouseData));
				break;
			}

			return 1;
		}
		return CallNextHookEx(nullptr, nCode, wParam, lParam);
	}

	void onTerminate(void* /*context*/)
	{
		LOG_INFO << "Terminating application via TerminateHotKey";
		TerminateProcess(GetCurrentProcess(), 0);
	}

	void resetKeyboardHook()
	{
		Gdi::GuiThread::execute([]()
			{
				if (g_keyboardHook)
				{
					UnhookWindowsHookEx(g_keyboardHook);
				}
				g_keyboardHook = CALL_ORIG_FUNC(SetWindowsHookExA)(
					WH_KEYBOARD_LL, &lowLevelKeyboardProc, Dll::g_currentModule, 0);
			});
	}

	void resetMouseHook()
	{
		Gdi::GuiThread::execute([]()
			{
				if (g_mouseHook)
				{
					UnhookWindowsHookEx(g_mouseHook);
				}
				g_mouseHook = CALL_ORIG_FUNC(SetWindowsHookExA)(
					WH_MOUSE_LL, &lowLevelMouseProc, Dll::g_currentModule, 0);
			});
	}

	HHOOK setWindowsHookEx(int idHook, HOOKPROC lpfn, HINSTANCE hmod, DWORD dwThreadId,
		decltype(&SetWindowsHookExA) origSetWindowsHookEx)
	{
		if (WH_KEYBOARD_LL == idHook && hmod && GetModuleHandle("AcGenral") == hmod)
		{
			// Disable the IgnoreAltTab shim
			return nullptr;
		}

		HHOOK result = origSetWindowsHookEx(idHook, lpfn, hmod, dwThreadId);
		if (result)
		{
			if (WH_KEYBOARD_LL == idHook)
			{
				resetKeyboardHook();
			}
			else if (WH_MOUSE_LL == idHook && g_mouseHook)
			{
				resetMouseHook();
			}
		}
		return result;
	}

	HHOOK WINAPI setWindowsHookExA(int idHook, HOOKPROC lpfn, HINSTANCE hmod, DWORD dwThreadId)
	{
		LOG_FUNC("SetWindowsHookExA", idHook, lpfn, hmod, Compat::hex(dwThreadId));
		return LOG_RESULT(setWindowsHookEx(idHook, lpfn, hmod, dwThreadId, CALL_ORIG_FUNC(SetWindowsHookExA)));
	}

	HHOOK WINAPI setWindowsHookExW(int idHook, HOOKPROC lpfn, HINSTANCE hmod, DWORD dwThreadId)
	{
		LOG_FUNC("SetWindowsHookExW", idHook, lpfn, hmod, Compat::hex(dwThreadId));
		return LOG_RESULT(setWindowsHookEx(idHook, lpfn, hmod, dwThreadId, CALL_ORIG_FUNC(SetWindowsHookExW)));
	}

	auto toTuple(const Input::HotKey& hotKey)
	{
		return std::make_tuple(hotKey.vk, hotKey.modifiers);
	}
}

namespace Input
{
	bool operator<(const HotKey& lhs, const HotKey& rhs)
	{
		return toTuple(lhs) < toTuple(rhs);
	}

	Overlay::Control* getCapture()
	{
		return g_capture;
	}

	Overlay::Window* getCaptureWindow()
	{
		return g_capture ? static_cast<Overlay::Window*>(&g_capture->getRoot()) : nullptr;
	}

	POINT getCursorPos()
	{
		return g_cursorPos;
	}

	HWND getCursorWindow()
	{
		return g_cursorWindow;
	}

	POINT getRelativeCursorPos()
	{
		auto captureWindow = Input::getCaptureWindow();
		if (!captureWindow)
		{
			return {};
		}

		const RECT rect = captureWindow->getRect();
		const int scaleFactor = captureWindow->getScaleFactor();

		auto cp = g_cursorPos;
		cp.x /= scaleFactor;
		cp.y /= scaleFactor;
		cp.x -= rect.left;
		cp.y -= rect.top;
		return cp;
	}

	void installHooks()
	{
		g_bmpArrow = CALL_ORIG_FUNC(LoadImageA)(Dll::g_currentModule, "BMP_ARROW", IMAGE_BITMAP, 0, 0, 0);

		BITMAP bm = {};
		GetObject(g_bmpArrow, sizeof(bm), &bm);
		g_bmpArrowSize = { bm.bmWidth, bm.bmHeight };

		HOOK_FUNCTION(user32, SetWindowsHookExA, setWindowsHookExA);
		HOOK_FUNCTION(user32, SetWindowsHookExW, setWindowsHookExW);

		registerHotKey(Config::terminateHotKey.get(), onTerminate, nullptr, false);
	}

	void registerHotKey(const HotKey& hotKey, std::function<void(void*)> action, void* context, bool onKeyDown)
	{
		if (0 != hotKey.vk)
		{
			g_hotKeys[hotKey] = { action, context, onKeyDown };
			if (!g_keyboardHook)
			{
				resetKeyboardHook();
			}
		}
	}

	void setCapture(Overlay::Control* control)
	{
		if (control && !control->isVisible())
		{
			control = nullptr;
		}
		g_capture = control;

		if (control)
		{
			auto window = getCaptureWindow();

			MONITORINFO mi = {};
			mi.cbSize = sizeof(mi);
			CALL_ORIG_FUNC(GetMonitorInfoA)(MonitorFromWindow(window->getWindow(), MONITOR_DEFAULTTOPRIMARY), &mi);
			g_monitorRect = mi.rcMonitor;

			if (!g_mouseHook)
			{
				g_cursorWindow = Gdi::PresentationWindow::create(window->getWindow());
				CALL_ORIG_FUNC(SetWindowLongA)(g_cursorWindow, GWL_WNDPROC, reinterpret_cast<LONG>(&cursorWindowProc));
				CALL_ORIG_FUNC(SetLayeredWindowAttributes)(g_cursorWindow, RGB(0xFF, 0xFF, 0xFF), 0, LWA_COLORKEY);

				g_cursorPos = { (g_monitorRect.left + g_monitorRect.right) / 2, (g_monitorRect.top + g_monitorRect.bottom) / 2 };
				CALL_ORIG_FUNC(SetWindowPos)(g_cursorWindow, DDraw::RealPrimarySurface::getTopmost(),
					g_cursorPos.x, g_cursorPos.y, g_bmpArrowSize.cx, g_bmpArrowSize.cy,
					SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_NOSENDCHANGING | SWP_SHOWWINDOW);
				g_capture->onMouseMove(getRelativeCursorPos());

				resetMouseHook();
			}
		}
		else if (g_mouseHook)
		{
			UnhookWindowsHookEx(g_mouseHook);
			g_mouseHook = nullptr;
			Gdi::GuiThread::destroyWindow(g_cursorWindow);
			g_cursorWindow = nullptr;
		}
	}

	void updateCursor()
	{
		Gdi::GuiThread::execute([]()
			{
				if (g_cursorWindow)
				{
					auto scaleFactor = Gdi::GuiThread::getConfigWindow()->getScaleFactor();
					CALL_ORIG_FUNC(SetWindowPos)(g_cursorWindow, DDraw::RealPrimarySurface::getTopmost(),
						g_cursorPos.x, g_cursorPos.y, g_bmpArrowSize.cx * scaleFactor, g_bmpArrowSize.cy * scaleFactor,
						SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_NOSENDCHANGING);
				}
			});
	}
}
