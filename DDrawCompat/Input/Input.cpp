#include <algorithm>
#include <map>
#include <tuple>

#include <Windows.h>
#include <hidusage.h>

#include <Common/Hook.h>
#include <Common/Log.h>
#include <Dll/Dll.h>
#include <DDraw/RealPrimarySurface.h>
#include <Gdi/PresentationWindow.h>
#include <Input/Input.h>
#include <Overlay/Window.h>

namespace
{
	struct HotKeyData
	{
		std::function<void(void*)> action;
		void* context;
	};

	const UINT WM_USER_HOTKEY = WM_USER;
	const UINT WM_USER_RESET_HOOK = WM_USER + 1;

	HANDLE g_bmpArrow = nullptr;
	SIZE g_bmpArrowSize = {};
	Overlay::Window* g_capture = nullptr;
	POINT g_cursorPos = {};
	HWND g_cursorWindow = nullptr;
	std::map<Input::HotKey, HotKeyData> g_hotKeys;
	RECT g_monitorRect = {};
	DWORD g_inputThreadId = 0;
	HHOOK g_keyboardHook = nullptr;
	HHOOK g_mouseHook = nullptr;

	LRESULT CALLBACK lowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam);
	LRESULT CALLBACK lowLevelMouseProc(int nCode, WPARAM wParam, LPARAM lParam);
	void setCursorPos(POINT cp);

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
			CALL_ORIG_FUNC(BitBlt)(ps.hdc, 0, 0, g_bmpArrowSize.cx, g_bmpArrowSize.cy, dc, 0, 0, SRCCOPY);
			SelectObject(dc, origBmp);
			DeleteDC(dc);
			EndPaint(hwnd, &ps);
			return 0;
		}

		case WM_WINDOWPOSCHANGED:
			DDraw::RealPrimarySurface::scheduleUpdate();
			break;
		}

		return CALL_ORIG_FUNC(DefWindowProcA)(hwnd, uMsg, wParam, lParam);
	}

	unsigned WINAPI inputThreadProc(LPVOID /*lpParameter*/)
	{
		g_inputThreadId = GetCurrentThreadId();
		g_bmpArrow = CALL_ORIG_FUNC(LoadImageA)(Dll::g_currentModule, "BMP_ARROW", IMAGE_BITMAP, 0, 0, 0);

		BITMAP bm = {};
		GetObject(g_bmpArrow, sizeof(bm), &bm);
		g_bmpArrowSize = { bm.bmWidth, bm.bmHeight };

		g_keyboardHook = CALL_ORIG_FUNC(SetWindowsHookExA)(WH_KEYBOARD_LL, &lowLevelKeyboardProc, Dll::g_currentModule, 0);

		MSG msg = {};
		while (GetMessage(&msg, nullptr, 0, 0))
		{
			if (msg.message == WM_TIMER && !msg.hwnd && msg.lParam)
			{
				reinterpret_cast<TIMERPROC>(msg.lParam)(nullptr, WM_TIMER, msg.wParam, 0);
			}
			if (!msg.hwnd)
			{
				if (WM_USER_HOTKEY == msg.message)
				{
					DWORD pid = 0;
					GetWindowThreadProcessId(GetForegroundWindow(), &pid);
					if (GetCurrentProcessId() == pid)
					{
						auto it = std::find_if(g_hotKeys.begin(), g_hotKeys.end(),
							[&](const auto& v) { return v.first.vk == msg.wParam; });
						if (it != g_hotKeys.end())
						{
							it->second.action(it->second.context);
						}
					}
				}
				else if (WM_USER_RESET_HOOK == msg.message)
				{
					if (msg.wParam == WH_KEYBOARD_LL)
					{
						UnhookWindowsHookEx(g_keyboardHook);
						g_keyboardHook = CALL_ORIG_FUNC(SetWindowsHookExA)(
							WH_KEYBOARD_LL, &lowLevelKeyboardProc, Dll::g_currentModule, 0);
					}
					else if (msg.wParam == WH_MOUSE_LL && g_mouseHook)
					{
						UnhookWindowsHookEx(g_mouseHook);
						g_mouseHook = CALL_ORIG_FUNC(SetWindowsHookExA)(
							WH_MOUSE_LL, &lowLevelMouseProc, Dll::g_currentModule, 0);
					}
				}
			}
			else
			{
				TranslateMessage(&msg);
				DispatchMessage(&msg);
			}
		}

		return 0;
	}

	LRESULT CALLBACK lowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam)
	{
		if (HC_ACTION == nCode && (WM_KEYDOWN == wParam || WM_SYSKEYDOWN == wParam))
		{
			auto llHook = reinterpret_cast<const KBDLLHOOKSTRUCT*>(lParam);
			PostThreadMessage(GetCurrentThreadId(), WM_USER_HOTKEY, llHook->vkCode, llHook->scanCode);
		}
		return CallNextHookEx(nullptr, nCode, wParam, lParam);
	}

	LRESULT CALLBACK lowLevelMouseProc(int nCode, WPARAM wParam, LPARAM lParam)
	{
		if (HC_ACTION == nCode)
		{
			POINT cp = g_cursorPos;
			POINT origCp = {};
			GetCursorPos(&origCp);

			auto& llHook = *reinterpret_cast<MSLLHOOKSTRUCT*>(lParam);
			cp.x += (llHook.pt.x - origCp.x);
			cp.y += (llHook.pt.y - origCp.y);
			cp.x = min(max(g_monitorRect.left, cp.x), g_monitorRect.right);
			cp.y = min(max(g_monitorRect.top, cp.y), g_monitorRect.bottom);
			setCursorPos(cp);

			RECT r = g_capture->getRect();
			cp.x -= r.left;
			cp.y -= r.top;

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
			}

			return 1;
		}
		return CallNextHookEx(nullptr, nCode, wParam, lParam);
	}

	void setCursorPos(POINT cp)
	{
		g_cursorPos = cp;
		CALL_ORIG_FUNC(SetWindowPos)(g_cursorWindow, HWND_TOPMOST, cp.x, cp.y, g_bmpArrowSize.cx, g_bmpArrowSize.cy,
			SWP_ASYNCWINDOWPOS | SWP_NOACTIVATE | SWP_NOSENDCHANGING);
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
		if (result && g_inputThreadId && (WH_KEYBOARD_LL == idHook || WH_MOUSE_LL == idHook))
		{
			PostThreadMessage(g_inputThreadId, WM_USER_RESET_HOOK, idHook, 0);
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

	Overlay::Window* getCapture()
	{
		return g_capture;
	}

	HWND getCursorWindow()
	{
		return g_cursorWindow;
	}

	void installHooks()
	{
		HOOK_FUNCTION(user32, SetWindowsHookExA, setWindowsHookExA);
		HOOK_FUNCTION(user32, SetWindowsHookExW, setWindowsHookExW);
	}

	void registerHotKey(const HotKey& hotKey, std::function<void(void*)> action, void* context)
	{
		static HANDLE thread = Dll::createThread(&inputThreadProc, nullptr, THREAD_PRIORITY_TIME_CRITICAL);
		if (thread)
		{
			g_hotKeys[hotKey] = { action, context };
		}
	}

	void setCapture(Overlay::Window* window)
	{
		g_capture = window;
		if (window)
		{
			if (!g_mouseHook)
			{
				g_cursorWindow = Gdi::PresentationWindow::create(window->getWindow(), &cursorWindowProc);
				CALL_ORIG_FUNC(SetLayeredWindowAttributes)(g_cursorWindow, RGB(0xFF, 0xFF, 0xFF), 0, LWA_COLORKEY);

				MONITORINFO mi = {};
				mi.cbSize = sizeof(mi);
				GetMonitorInfo(MonitorFromWindow(window->getWindow(), MONITOR_DEFAULTTOPRIMARY), &mi);
				g_monitorRect = mi.rcMonitor;

				RECT r = window->getRect();
				g_cursorPos = { (r.left + r.right) / 2, (r.top + r.bottom) / 2 };
				CALL_ORIG_FUNC(SetWindowPos)(g_cursorWindow, HWND_TOPMOST, g_cursorPos.x, g_cursorPos.y,
					g_bmpArrowSize.cx, g_bmpArrowSize.cy, SWP_NOACTIVATE | SWP_NOSENDCHANGING);
				ShowWindow(g_cursorWindow, SW_SHOW);

				g_mouseHook = CALL_ORIG_FUNC(SetWindowsHookExA)(WH_MOUSE_LL, &lowLevelMouseProc, Dll::g_currentModule, 0);
			}
		}
		else if (g_mouseHook)
		{
			UnhookWindowsHookEx(g_mouseHook);
			g_mouseHook = nullptr;
			PostMessage(g_cursorWindow, WM_CLOSE, 0, 0);
			g_cursorWindow = nullptr;
		}
	}
}
