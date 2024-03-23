#include <algorithm>
#include <map>
#include <tuple>

#include <Windows.h>
#include <winternl.h>

#include <Common/BitSet.h>
#include <Common/Hook.h>
#include <Common/Log.h>
#include <Common/Path.h>
#include <Common/Rect.h>
#include <Config/Settings/TerminateHotKey.h>
#include <Dll/Dll.h>
#include <DDraw/RealPrimarySurface.h>
#include <Gdi/GuiThread.h>
#include <Gdi/PresentationWindow.h>
#include <Input/Input.h>
#include <Overlay/ConfigWindow.h>
#include <Overlay/Window.h>
#include <Win32/DisplayMode.h>

namespace
{
	struct DInputMouseHookData
	{
		HOOKPROC origHookProc;
		LPARAM origHookStruct;
		MSLLHOOKSTRUCT hookStruct;
		DWORD dpiScale;
	};

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
	POINT g_origCursorPos = { MAXLONG, MAXLONG };
	HWND g_cursorWindow = nullptr;
	std::map<Input::HotKey, HotKeyData> g_hotKeys;
	RECT g_monitorRect = {};
	HHOOK g_keyboardHook = nullptr;
	HHOOK g_mouseHook = nullptr;
	BitSet<VK_LBUTTON, VK_OEM_CLEAR> g_keyState;

	DInputMouseHookData g_dinputMouseHookData = {};
	decltype(&PhysicalToLogicalPointForPerMonitorDPI) g_physicalToLogicalPointForPerMonitorDPI = nullptr;

	LRESULT CALLBACK lowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam);
	LRESULT CALLBACK lowLevelMouseProc(int nCode, WPARAM wParam, LPARAM lParam);
	POINT physicalToLogicalPoint(POINT pt, DWORD dpiScale);

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

	LRESULT WINAPI dinputCallNextHookEx(HHOOK hhk, int nCode, WPARAM wParam, LPARAM lParam)
	{
		if (lParam == reinterpret_cast<LPARAM>(&g_dinputMouseHookData.hookStruct))
		{
			lParam = g_dinputMouseHookData.origHookStruct;
		}
		return CallNextHookEx(hhk, nCode, wParam, lParam);
	}

	LRESULT CALLBACK dinputLowLevelMouseProc(int nCode, WPARAM wParam, LPARAM lParam)
	{
		if (HC_ACTION == nCode)
		{
			auto& data = g_dinputMouseHookData;
			data.origHookStruct = lParam;
			data.hookStruct = *reinterpret_cast<MSLLHOOKSTRUCT*>(lParam);

			if (WM_MOUSEMOVE == wParam)
			{
				data.hookStruct.pt = physicalToLogicalPoint(data.hookStruct.pt, data.dpiScale);
			}
			else
			{
				CALL_ORIG_FUNC(GetCursorPos)(&data.hookStruct.pt);
			}

			lParam = reinterpret_cast<LPARAM>(&g_dinputMouseHookData.hookStruct);
		}
		return g_dinputMouseHookData.origHookProc(nCode, wParam, lParam);
	}

	DWORD getDpiScaleForCursorPos()
	{
		POINT cp = {};
		CALL_ORIG_FUNC(GetCursorPos)(&cp);
		return Win32::DisplayMode::getMonitorInfo(cp).dpiScale;
	}

	LRESULT CALLBACK lowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam)
	{
		if (HC_ACTION == nCode &&
			(WM_KEYDOWN == wParam || WM_KEYUP == wParam || WM_SYSKEYDOWN == wParam || WM_SYSKEYUP == wParam))
		{
			auto llHook = reinterpret_cast<const KBDLLHOOKSTRUCT*>(lParam);
			if (static_cast<int>(llHook->vkCode) >= g_keyState.getMin() &&
				static_cast<int>(llHook->vkCode) <= g_keyState.getMax())
			{
				if (WM_KEYDOWN == wParam || WM_SYSKEYDOWN == wParam)
				{
					g_keyState.set(llHook->vkCode);
				}
				else
				{
					g_keyState.reset(llHook->vkCode);
				}
			}

			DWORD pid = 0;
			GetWindowThreadProcessId(GetForegroundWindow(), &pid);
			if (GetCurrentProcessId() == pid)
			{
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
				if (MAXLONG == g_origCursorPos.y)
				{
					if (llHook.flags & LLMHF_INJECTED)
					{
						if (MAXLONG == g_origCursorPos.x)
						{
							g_origCursorPos.x = llHook.pt.x;
						}
						else
						{
							g_origCursorPos.y = llHook.pt.y;
						}
					}
					return 1;
				}

				POINT cp = g_cursorPos;
				cp.x += llHook.pt.x - g_origCursorPos.x;
				cp.y += llHook.pt.y - g_origCursorPos.y;
				cp.x = std::min(std::max(g_monitorRect.left, cp.x), g_monitorRect.right);
				cp.y = std::min(std::max(g_monitorRect.top, cp.y), g_monitorRect.bottom);
				g_cursorPos = cp;
				DDraw::RealPrimarySurface::scheduleOverlayUpdate();
			}

			if (!g_capture->isEnabled())
			{
				return 1;
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

	POINT physicalToLogicalPoint(POINT pt, DWORD dpiScale)
	{
		if (g_physicalToLogicalPointForPerMonitorDPI)
		{
			g_physicalToLogicalPointForPerMonitorDPI(nullptr, &pt);
			return pt;
		}
		return { MulDiv(pt.x, 100, dpiScale), MulDiv(pt.y, 100, dpiScale) };
	}

	void resetKeyboardHook()
	{
		Gdi::GuiThread::execute([]()
			{
				if (g_keyboardHook)
				{
					UnhookWindowsHookEx(g_keyboardHook);
				}

				g_keyState.reset();
				g_keyboardHook = CALL_ORIG_FUNC(SetWindowsHookExA)(
					WH_KEYBOARD_LL, &lowLevelKeyboardProc, Dll::g_currentModule, 0);
				if (!g_keyboardHook)
				{
					LOG_ONCE("ERROR: Failed to install low level keyboard hook, error code: " << GetLastError());
				}
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

				g_origCursorPos = { MAXLONG, MAXLONG };
				g_mouseHook = CALL_ORIG_FUNC(SetWindowsHookExA)(
					WH_MOUSE_LL, &lowLevelMouseProc, Dll::g_currentModule, 0);

				if (g_mouseHook)
				{
					INPUT inputs[2] = {};
					inputs[0].mi.dy = 1;
					inputs[0].mi.dwFlags = MOUSEEVENTF_MOVE;
					inputs[1].mi.dx = 1;
					inputs[1].mi.dwFlags = MOUSEEVENTF_MOVE;
					SendInput(2, inputs, sizeof(INPUT));
				}
				else
				{
					LOG_ONCE("ERROR: Failed to install low level mouse hook, error code: " << GetLastError());
				}
			});
	}

	BOOL WINAPI setCursorPos(int X, int Y)
	{
		LOG_FUNC("SetCursorPos", X, Y);
		auto result = CALL_ORIG_FUNC(SetCursorPos)(X, Y);
		if (result && g_mouseHook)
		{
			resetMouseHook();
		}
		return LOG_RESULT(result);
	}

	HHOOK setWindowsHookEx(int idHook, HOOKPROC lpfn, HINSTANCE hmod, DWORD dwThreadId,
		decltype(&SetWindowsHookExA) origSetWindowsHookEx)
	{
		if (hmod && (WH_KEYBOARD_LL == idHook || WH_MOUSE_LL == idHook))
		{
			auto moduleName = Compat::getModulePath(hmod).stem().string();
			if (WH_KEYBOARD_LL == idHook && 0 == _stricmp(moduleName.c_str(), "acgenral"))
			{
				// Disable the IgnoreAltTab shim
				return nullptr;
			}
			
			if (WH_MOUSE_LL == idHook &&
				(0 == _stricmp(moduleName.c_str(), "dinput") || 0 == _stricmp(moduleName.c_str(), "dinput8")))
			{
				g_dinputMouseHookData.origHookProc = lpfn;
				if (!g_physicalToLogicalPointForPerMonitorDPI)
				{
					g_dinputMouseHookData.dpiScale = getDpiScaleForCursorPos();
				}

				lpfn = dinputLowLevelMouseProc;
				Compat::hookIatFunction(hmod, "CallNextHookEx", dinputCallNextHookEx);
			}
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

		g_physicalToLogicalPointForPerMonitorDPI = reinterpret_cast<decltype(&PhysicalToLogicalPointForPerMonitorDPI)>(
			GetProcAddress(GetModuleHandle("user32"), "PhysicalToLogicalPointForPerMonitorDPI"));

		HOOK_FUNCTION(user32, SetCursorPos, setCursorPos);
		HOOK_FUNCTION(user32, SetWindowsHookExA, setWindowsHookExA);
		HOOK_FUNCTION(user32, SetWindowsHookExW, setWindowsHookExW);

		registerHotKey(Config::terminateHotKey.get(), onTerminate, nullptr, false);
	}

	bool isKeyDown(int vk)
	{
		switch (vk)
		{
		case VK_SHIFT:
			return g_keyState.test(VK_LSHIFT) || g_keyState.test(VK_RSHIFT);
		case VK_CONTROL:
			return g_keyState.test(VK_LCONTROL) || g_keyState.test(VK_RCONTROL);
		case VK_MENU:
			return g_keyState.test(VK_LMENU) || g_keyState.test(VK_RMENU);
		}

		if (vk >= g_keyState.getMin() && vk <= g_keyState.getMax())
		{
			return g_keyState.test(vk);
		}
		return false;
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
			g_monitorRect = Win32::DisplayMode::getMonitorInfo(window->getWindow()).rcMonitor;

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
