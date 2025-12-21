#include <algorithm>
#include <map>
#include <tuple>
#include <vector>

#include <Windows.h>
#include <hidusage.h>
#include <mmsystem.h>
#include <winternl.h>

#include <Common/BitSet.h>
#include <Common/Hook.h>
#include <Common/Log.h>
#include <Common/Path.h>
#include <Common/Rect.h>
#include <Common/ScopedCriticalSection.h>
#include <Config/Settings/MousePollingRate.h>
#include <Config/Settings/MouseSensitivity.h>
#include <Config/Settings/TerminateHotKey.h>
#include <Dll/Dll.h>
#include <DDraw/RealPrimarySurface.h>
#include <Gdi/GuiThread.h>
#include <Gdi/PresentationWindow.h>
#include <Input/Input.h>
#include <Overlay/ConfigWindow.h>
#include <Overlay/Steam.h>
#include <Overlay/Window.h>
#include <Win32/DisplayMode.h>
#include <Win32/DpiAwareness.h>

namespace
{
	const UINT WM_USER_SEND_MOUSE_MOVE = WM_USER;

	struct DInputMouseHookData
	{
		HOOKPROC origHookProc;
		LPARAM origHookStruct;
		MSLLHOOKSTRUCT hookStruct;
	};

	struct HotKeyData
	{
		std::function<void(void*)> action;
		void* context;
		bool onKeyDown;
	};

	struct MouseScaleParams
	{
		long long multiplier;
		POINT position;
		POINT remainder;
		SIZE resolution;
		bool useRaw;
	};

	HANDLE g_bmpArrow = nullptr;
	SIZE g_bmpArrowSize = {};
	Overlay::Control* g_capture = nullptr;
	POINT g_cursorPos = {};
	HWND g_cursorWindow = nullptr;
	HWND g_inputWindow = nullptr;
	Win32::DisplayMode::MonitorInfo g_fullscreenMonitorInfo = {};
	MouseScaleParams g_mouseScale = {};
	std::map<Input::HotKey, HotKeyData> g_hotKeys;
	RECT g_monitorRect = {};
	HHOOK g_keyboardHook = nullptr;
	HHOOK g_mouseHook = nullptr;
	UINT g_mousePollingRate = 0;
	UINT g_mousePollingTimerId = 0;
	Config::AtomicSettingStore g_mouseSensitivity(Config::mouseSensitivity);
	BitSet<VK_LBUTTON, VK_OEM_CLEAR> g_keyState;

	DInputMouseHookData g_dinputMouseHookData = {};
	decltype(&PhysicalToLogicalPointForPerMonitorDPI) g_physicalToLogicalPointForPerMonitorDPI = nullptr;

	Compat::CriticalSection g_rawInputCs;

	LRESULT CALLBACK lowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam);
	LRESULT CALLBACK lowLevelMouseProc(int nCode, WPARAM wParam, LPARAM lParam);
	POINT physicalToLogicalPoint(POINT pt);
	void processRawInput(HRAWINPUT ri);
	void sendMouseMove();

	HHOOK addHook(int idHook, HOOKPROC lpfn)
	{
		HHOOK hook = CALL_ORIG_FUNC(SetWindowsHookExA)(idHook, lpfn, Dll::g_origDDrawModule, 0);
		if (!hook)
		{
			if (WH_KEYBOARD_LL == idHook)
			{
				LOG_ONCE("ERROR: Failed to install low level keyboard hook, error code: " << GetLastError());
			}
			else
			{
				LOG_ONCE("ERROR: Failed to install low level mouse hook, error code: " << GetLastError());
			}
		}
		return hook;
	}

	void addMouseMove(LONG x, LONG y)
	{
		if (POINT{} == g_mouseScale.position &&
			Config::Settings::MousePollingRate::NATIVE == g_mousePollingRate)
		{
			PostMessage(g_inputWindow, WM_USER_SEND_MOUSE_MOVE, 0, 0);
		}
		g_mouseScale.position.x += x;
		g_mouseScale.position.y += y;
	}

	void addMouseMove(const MSLLHOOKSTRUCT& llHook)
	{
		Win32::ScopedDpiAwareness dpiAwareness;
		POINT origCursorPos = {};
		CALL_ORIG_FUNC(GetCursorPos)(&origCursorPos);
		addMouseMove(llHook.pt.x - origCursorPos.x, llHook.pt.y - origCursorPos.y);
	}

	void addMouseMove(const RAWINPUT& ri)
	{
		if ((!(ri.data.mouse.usFlags & MOUSE_MOVE_ABSOLUTE) || (ri.data.mouse.usFlags & MOUSE_VIRTUAL_DESKTOP)) &&
			(0 != ri.data.mouse.lLastX || 0 != ri.data.mouse.lLastY))
		{
			addMouseMove(ri.data.mouse.lLastX, ri.data.mouse.lLastY);
		}
	}

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
			CALL_ORIG_FUNC(GetWindowRect)(hwnd, &wr);
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
				if (Win32::DpiAwareness::isMixedModeSupported())
				{
					POINT logicalCursorPos = {};
					GetCursorPos(&logicalCursorPos);

					Win32::ScopedDpiAwareness dpiAwareness;
					POINT physicalCursorPos = {};
					GetCursorPos(&physicalCursorPos);

					data.hookStruct.pt.x = logicalCursorPos.x + data.hookStruct.pt.x - physicalCursorPos.x;
					data.hookStruct.pt.y = logicalCursorPos.y + data.hookStruct.pt.y - physicalCursorPos.y;
				}
				else
				{
					data.hookStruct.pt = physicalToLogicalPoint(data.hookStruct.pt);
				}
			}
			else
			{
				CALL_ORIG_FUNC(GetCursorPos)(&data.hookStruct.pt);
			}

			lParam = reinterpret_cast<LPARAM>(&g_dinputMouseHookData.hookStruct);
		}
		return g_dinputMouseHookData.origHookProc(nCode, wParam, lParam);
	}

	BOOL dinputSystemParametersInfoW(UINT uiAction, UINT uiParam, PVOID pvParam, UINT fWinIni)
	{
		LOG_FUNC("dinputSystemParametersInfoW", Compat::hex(uiAction), uiParam, pvParam, fWinIni);
		auto result = CALL_ORIG_FUNC(SystemParametersInfoW)(uiAction, uiParam, pvParam, fWinIni);
		if (result && SPI_GETMOUSE == uiAction)
		{
			memset(pvParam, 0, 3 * sizeof(int));
		}
		return LOG_RESULT(result);
	}

	bool getMouseAccel()
	{
		int mouse[3] = {};
		CALL_ORIG_FUNC(SystemParametersInfoA)(SPI_GETMOUSE, 0, &mouse, 0);
		return mouse[2];
	}

	int getMouseSpeedFactor()
	{
		int speed = 10;
		CALL_ORIG_FUNC(SystemParametersInfoA)(SPI_GETMOUSESPEED, 0, &speed, 0);
		if (speed < 1 || speed > 20)
		{
			speed = 10;
		}

		if (speed <= 2)
		{
			return speed;
		}
		if (speed <= 10)
		{
			return (speed - 2) * 4;
		}
		return (speed - 6) * 8;
	}

	UINT WINAPI getRawInputBuffer(PRAWINPUT pData, PUINT pcbSize, UINT cbSizeHeader)
	{
		int result = CALL_ORIG_FUNC(GetRawInputBuffer)(pData, pcbSize, cbSizeHeader);
		if (g_capture)
		{
			for (int i = 0; i < result; ++i)
			{
				if (RIM_TYPEMOUSE == pData->header.dwType)
				{
					pData->data.mouse = {};
				}
				pData = NEXTRAWINPUTBLOCK(pData);
			}
		}
		return result;
	}

	UINT WINAPI getRawInputData(HRAWINPUT hRawInput, UINT uiCommand, LPVOID pData, PUINT pcbSize, UINT cbSizeHeader)
	{
		int result = CALL_ORIG_FUNC(GetRawInputData)(hRawInput, uiCommand, pData, pcbSize, cbSizeHeader);
		if (g_capture && result >= sizeof(RAWINPUT))
		{
			auto& ri = *static_cast<RAWINPUT*>(pData);
			if (RIM_TYPEMOUSE == ri.header.dwType)
			{
				ri.data.mouse = {};
			}
		}
		return result;
	}

	RAWINPUTDEVICE getRegisteredRawMouseDevice()
	{
		UINT numDevices = 0;
		GetRegisteredRawInputDevices(nullptr, &numDevices, sizeof(RAWINPUTDEVICE));
		if (0 == numDevices)
		{
			return {};
		}

		std::vector<RAWINPUTDEVICE> devices(numDevices);
		const int count = GetRegisteredRawInputDevices(devices.data(), &numDevices, sizeof(RAWINPUTDEVICE));
		for (int i = 0; i < count; ++i)
		{
			if (HID_USAGE_PAGE_GENERIC == devices[i].usUsagePage &&
				HID_USAGE_GENERIC_MOUSE == devices[i].usUsage &&
				!(devices[i].dwFlags & RIDEV_EXCLUDE))
			{
				return devices[i];
			}
		}
		return {};
	}

	LRESULT CALLBACK inputWindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
	{
		switch (uMsg)
		{
		case WM_INPUT:
			processRawInput(reinterpret_cast<HRAWINPUT>(lParam));
			break;

		case WM_USER_SEND_MOUSE_MOVE:
			sendMouseMove();
			return 0;
		}

		return CALL_ORIG_FUNC(DefWindowProc)(hwnd, uMsg, wParam, lParam);
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

				auto steamWindow = Overlay::Steam::getWindow();
				if (steamWindow)
				{
					PostMessage(steamWindow, wParam, llHook->vkCode, 0);
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

			if (!g_capture)
			{
				if (0 != g_mouseScale.multiplier && !(llHook.flags & LLMHF_INJECTED) && WM_MOUSEMOVE == wParam)
				{
					if (!g_mouseScale.useRaw)
					{
						addMouseMove(llHook);
					}
					return 1;
				}
				return CallNextHookEx(nullptr, nCode, wParam, lParam);
			}

			if (WM_MOUSEMOVE == wParam)
			{
				POINT origCursorPos = {};
				POINT newCursorPos = llHook.pt;
				if (Win32::DpiAwareness::isMixedModeSupported())
				{
					Win32::ScopedDpiAwareness dpiAwareness;
					CALL_ORIG_FUNC(GetCursorPos)(&origCursorPos);
				}
				else
				{
					CALL_ORIG_FUNC(GetCursorPos)(&origCursorPos);
					newCursorPos = physicalToLogicalPoint(llHook.pt);
				}

				POINT cp = g_cursorPos;
				cp.x += newCursorPos.x - origCursorPos.x;
				cp.y += newCursorPos.y - origCursorPos.y;
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

	void CALLBACK onMousePollingRateTimer(
		UINT /*uTimerID*/, UINT /*uMsg*/, DWORD_PTR /*dwUser*/, DWORD_PTR /*dw1*/, DWORD_PTR /*dw2*/)
	{
		PostMessage(g_inputWindow, WM_USER_SEND_MOUSE_MOVE, 0, 0);
	}

	void onTerminate(void* /*context*/)
	{
		LOG_INFO << "Terminating application via TerminateHotKey";
		TerminateProcess(GetCurrentProcess(), 0);
	}

	POINT physicalToLogicalPoint(POINT pt)
	{
		if (g_physicalToLogicalPointForPerMonitorDPI)
		{
			g_physicalToLogicalPointForPerMonitorDPI(nullptr, &pt);
			return pt;
		}
		auto dpiScale = Win32::DisplayMode::getMonitorInfo(pt).dpiScale;
		return { MulDiv(pt.x, 100, dpiScale), MulDiv(pt.y, 100, dpiScale) };
	}

	void processRawInput(HRAWINPUT rawInput)
	{
		RAWINPUT ri = {};
		UINT size = sizeof(ri);
		if (-1 != CALL_ORIG_FUNC(GetRawInputData)(rawInput, RID_INPUT, &ri, &size, sizeof(RAWINPUTHEADER)))
		{
			addMouseMove(ri);
		}

		alignas(8) RAWINPUT buf[64];
		size = sizeof(buf);
		int count = CALL_ORIG_FUNC(GetRawInputBuffer)(buf, &size, sizeof(RAWINPUTHEADER));
		while (count > 0)
		{
			RAWINPUT* p = buf;
			for (int i = 0; i < count; ++i)
			{
				addMouseMove(*p);
				p = NEXTRAWINPUTBLOCK(p);
			}
			count = CALL_ORIG_FUNC(GetRawInputBuffer)(buf, &size, sizeof(RAWINPUTHEADER));
		}
	}

	void removeHook(HHOOK& hook)
	{
		if (hook)
		{
			CALL_ORIG_FUNC(UnhookWindowsHookEx)(hook);
			hook = nullptr;
		}
	}

	BOOL WINAPI registerRawInputDevices(PCRAWINPUTDEVICE pRawInputDevices, UINT uiNumDevices, UINT cbSize)
	{
		LOG_FUNC("RegisterRawInputDevices", Compat::array(pRawInputDevices, uiNumDevices), uiNumDevices, cbSize);
		if (1 == uiNumDevices &&
			HID_USAGE_PAGE_GENERIC == pRawInputDevices->usUsagePage &&
			(HID_USAGE_GENERIC_MOUSE == pRawInputDevices->usUsage || HID_USAGE_GENERIC_KEYBOARD == pRawInputDevices->usUsage) &&
			pRawInputDevices->hwndTarget)
		{
			char className[32] = {};
			GetClassNameA(pRawInputDevices->hwndTarget, className, sizeof(className));
			if (0 == strcmp(className, "DIEmWin"))
			{
				return LOG_RESULT(FALSE);
			}
		}

		Compat::ScopedCriticalSection lock(g_rawInputCs);
		BOOL result = CALL_ORIG_FUNC(RegisterRawInputDevices)(pRawInputDevices, uiNumDevices, cbSize);
		if (result)
		{
			Gdi::GuiThread::executeAsyncFunc(Input::updateMouseSensitivity);
		}
		return LOG_RESULT(result);
	}

	void resetKeyboardHookAsync()
	{
		removeHook(g_keyboardHook);
		g_keyState.reset();
		g_keyboardHook = addHook(WH_KEYBOARD_LL, &lowLevelKeyboardProc);
	}

	void resetKeyboardHook()
	{
		Gdi::GuiThread::executeAsyncFunc(resetKeyboardHookAsync);
	}

	void resetMouseHookAsync()
	{
		removeHook(g_mouseHook);
		Input::updateMouseSensitivity();
	}

	void resetMouseHook()
	{
		Gdi::GuiThread::executeAsyncFunc(resetMouseHookAsync);
	}

	void sendMouseMove()
	{
		if (POINT{} == g_mouseScale.position || SIZE{} == g_mouseScale.resolution)
		{
			return;
		}

		const auto relX = g_mouseScale.position.x * g_mouseScale.multiplier + g_mouseScale.remainder.x;
		const auto relY = g_mouseScale.position.y * g_mouseScale.multiplier + g_mouseScale.remainder.y;
		g_mouseScale.remainder.x = relX < 0 ? -(-relX & 0xFFFF) : (relX & 0xFFFF);
		g_mouseScale.remainder.y = relY < 0 ? -(-relY & 0xFFFF) : (relY & 0xFFFF);
		g_mouseScale.position = {};

		Win32::ScopedDpiAwareness dpiAwareness;
		POINT origCursorPos = {};
		CALL_ORIG_FUNC(GetCursorPos)(&origCursorPos);

		const auto newX = origCursorPos.x + (relX < 0 ? -(-relX >> 16) : (relX >> 16));
		const auto newY = origCursorPos.y + (relY < 0 ? -(-relY >> 16) : (relY >> 16));

		INPUT input = {};
		input.mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE;
		input.mi.dx = static_cast<LONG>((newX * 65536 + (newX < 0 ? -32768 : 32768)) / g_mouseScale.resolution.cx);
		input.mi.dy = static_cast<LONG>((newY * 65536 + (newY < 0 ? -32768 : 32768)) / g_mouseScale.resolution.cy);
		SendInput(1, &input, sizeof(INPUT));
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
				lpfn = dinputLowLevelMouseProc;
				Compat::hookIatFunction(hmod, "CallNextHookEx", dinputCallNextHookEx);
				Compat::hookIatFunction(hmod, "SystemParametersInfoW", dinputSystemParametersInfoW);
			}
		}

		HHOOK result = origSetWindowsHookEx(idHook, lpfn, hmod, dwThreadId);
		if (result)
		{
			if (WH_KEYBOARD_LL == idHook)
			{
				resetKeyboardHook();
			}
			else if (WH_MOUSE_LL == idHook)
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

	void updateMouseHooks()
	{
		const UINT mousePollingRate = IsRectEmpty(&g_fullscreenMonitorInfo.rcReal)
			? Config::Settings::MousePollingRate::NATIVE
			: Config::mousePollingRate.get();
		if (g_mousePollingRate != mousePollingRate)
		{
			g_mousePollingRate = mousePollingRate;
			if (Config::Settings::MousePollingRate::NATIVE != g_mousePollingRate)
			{
				g_mousePollingTimerId = timeSetEvent(1000 / g_mousePollingRate, 1, &onMousePollingRateTimer, 0,
					TIME_PERIODIC | TIME_KILL_SYNCHRONOUS);
				if (0 == g_mousePollingTimerId)
				{
					LOG_ONCE("ERROR: Failed to set timer for MousePollingRate: " << GetLastError());
					g_mousePollingRate = Config::Settings::MousePollingRate::NATIVE;
				}
			}
			else if (0 != g_mousePollingTimerId)
			{
				timeKillEvent(g_mousePollingTimerId);
				g_mousePollingTimerId = 0;
			}
		}

		if (0 != g_mousePollingTimerId && 0 == g_mouseScale.multiplier)
		{
			g_mouseScale.multiplier = 65536;
			g_mouseScale.resolution = Rect::getSize(g_fullscreenMonitorInfo.rcReal);
		}

		const bool isLowLevelHookNeeded = g_capture || 0 != g_mouseScale.multiplier;
		if (!isLowLevelHookNeeded)
		{
			removeHook(g_mouseHook);
		}
		else if (!g_mouseHook)
		{
			g_mouseHook = addHook(WH_MOUSE_LL, &lowLevelMouseProc);
			if (!g_mouseHook)
			{
				g_mouseScale.useRaw = false;
				if (0 != g_mousePollingTimerId)
				{
					timeKillEvent(g_mousePollingTimerId);
					g_mousePollingTimerId = 0;
					g_mousePollingRate = Config::Settings::MousePollingRate::NATIVE;
				}
			}
		}

		if (!g_inputWindow)
		{
			return;
		}

		Compat::ScopedCriticalSection lock(g_rawInputCs);
		const auto rawMouseDevice = getRegisteredRawMouseDevice();
		if (g_mouseScale.useRaw)
		{
			if (HID_USAGE_GENERIC_MOUSE != rawMouseDevice.usUsage)
			{
				RAWINPUTDEVICE rid = {};
				rid.usUsagePage = HID_USAGE_PAGE_GENERIC;
				rid.usUsage = HID_USAGE_GENERIC_MOUSE;
				rid.hwndTarget = g_inputWindow;
				g_mouseScale.useRaw = CALL_ORIG_FUNC(RegisterRawInputDevices)(&rid, 1, sizeof(RAWINPUTDEVICE));
			}
		}
		else if (g_inputWindow == rawMouseDevice.hwndTarget)
		{
			RAWINPUTDEVICE rid = {};
			rid.usUsagePage = HID_USAGE_PAGE_GENERIC;
			rid.usUsage = HID_USAGE_GENERIC_MOUSE;
			rid.dwFlags = RIDEV_REMOVE;
			CALL_ORIG_FUNC(RegisterRawInputDevices)(&rid, 1, sizeof(RAWINPUTDEVICE));
		}
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

	void init()
	{
		WNDCLASS wc = {};
		wc.lpfnWndProc = &inputWindowProc;
		wc.hInstance = Dll::g_currentModule;
		wc.lpszClassName = "DDrawCompatInputWindow";
		CALL_ORIG_FUNC(RegisterClassA)(&wc);

		g_inputWindow = CreateWindowExA(
			0, "DDrawCompatInputWindow", nullptr, WS_POPUP, 0, 0, 0, 0, nullptr, nullptr, nullptr, nullptr);
		if (!g_inputWindow)
		{
			LOG_INFO << "ERROR: Failed to create input window";
		}

		registerHotKey(Config::terminateHotKey.get(), onTerminate, nullptr, false);
	}

	void installHooks()
	{
		g_bmpArrow = CALL_ORIG_FUNC(LoadImageA)(Dll::g_currentModule, "BMP_ARROW", IMAGE_BITMAP, 0, 0, 0);

		BITMAP bm = {};
		GetObject(g_bmpArrow, sizeof(bm), &bm);
		g_bmpArrowSize = { bm.bmWidth, bm.bmHeight };

		g_physicalToLogicalPointForPerMonitorDPI = GET_PROC_ADDRESS(user32, PhysicalToLogicalPointForPerMonitorDPI);

		HOOK_FUNCTION(user32, GetRawInputData, getRawInputData);
		HOOK_FUNCTION(user32, GetRawInputBuffer, getRawInputBuffer);
		HOOK_FUNCTION(user32, RegisterRawInputDevices, registerRawInputDevices);
		HOOK_FUNCTION(user32, SetWindowsHookExA, setWindowsHookExA);
		HOOK_FUNCTION(user32, SetWindowsHookExW, setWindowsHookExW);
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

			if (!g_cursorWindow)
			{
				g_cursorWindow = Gdi::PresentationWindow::create(window->getWindow());
				CALL_ORIG_FUNC(SetWindowLongA)(g_cursorWindow, GWL_WNDPROC, reinterpret_cast<LONG>(&cursorWindowProc));
				CALL_ORIG_FUNC(SetLayeredWindowAttributes)(g_cursorWindow, RGB(0xFF, 0xFF, 0xFF), 0, LWA_COLORKEY);

				g_cursorPos = { (g_monitorRect.left + g_monitorRect.right) / 2, (g_monitorRect.top + g_monitorRect.bottom) / 2 };
				CALL_ORIG_FUNC(SetWindowPos)(g_cursorWindow, DDraw::RealPrimarySurface::getTopmost(),
					g_cursorPos.x, g_cursorPos.y, g_bmpArrowSize.cx, g_bmpArrowSize.cy,
					SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_NOSENDCHANGING | SWP_SHOWWINDOW);
				g_capture->onMouseMove(getRelativeCursorPos());
			}
		}
		else if (g_cursorWindow)
		{
			Gdi::GuiThread::destroyWindow(g_cursorWindow);
			g_cursorWindow = nullptr;
		}

		updateMouseSensitivity();
	}

	void setFullscreenMonitorInfo(const Win32::DisplayMode::MonitorInfo& mi)
	{
		if (!Gdi::GuiThread::isReady())
		{
			g_fullscreenMonitorInfo = mi;
			return;
		}

		Gdi::GuiThread::execute([&]()
			{
				g_fullscreenMonitorInfo = mi;
				updateMouseSensitivity();
			});
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

	void updateMouseSensitivity()
	{
		Gdi::GuiThread::execute([]()
			{
				g_mouseScale = {};
				const auto mouseSensitivity = g_mouseSensitivity.get();

				if (Config::Settings::MouseSensitivity::NATIVE == mouseSensitivity.value ||
					IsRectEmpty(&g_fullscreenMonitorInfo.rcEmulated) ||
					Overlay::Steam::isOverlayOpen())
				{
					updateMouseHooks();
					return;
				}

				Compat::ScopedCriticalSection lock(g_rawInputCs);
				const auto rawMouseDevice = getRegisteredRawMouseDevice();
				if (HID_USAGE_GENERIC_MOUSE == rawMouseDevice.usUsage && g_inputWindow != rawMouseDevice.hwndTarget)
				{
					updateMouseHooks();
					return;
				}

				g_mouseScale.useRaw = !g_capture &&
					(Config::Settings::MouseSensitivity::NOACCEL == mouseSensitivity.value ||
						g_fullscreenMonitorInfo.rcDpiAware != g_fullscreenMonitorInfo.rcReal ||
						!getMouseAccel());

				const auto& desktopMi = Win32::DisplayMode::getDesktopMonitorInfo();
				const auto desktopSize = Rect::getSize(desktopMi.rcReal);
				const auto emulatedSize = Rect::getSize(g_fullscreenMonitorInfo.rcEmulated);
				const auto size = (emulatedSize.cx * desktopSize.cy > emulatedSize.cy * desktopSize.cx) ? &SIZE::cx : &SIZE::cy;

				long long num = emulatedSize.*size * g_fullscreenMonitorInfo.dpiScale;
				long long denom = desktopSize.*size * 100;

				num *= mouseSensitivity.param;
				denom *= 100;

				g_mouseScale.multiplier = num;
				g_mouseScale.resolution = Rect::getSize(g_fullscreenMonitorInfo.rcReal);
				updateMouseHooks();

				if (g_mouseScale.useRaw)
				{
					num *= getMouseSpeedFactor() * desktopMi.realDpiScale;
					denom *= 32 * 100;
				}
				else
				{
					num *= desktopMi.realDpiScale;
					denom *= g_fullscreenMonitorInfo.realDpiScale;
				}

				g_mouseScale.multiplier = num * 65536 / denom;
			});
	}

	void updateMouseSensitivitySetting()
	{
		g_mouseSensitivity.update();
		updateMouseSensitivity();
	}
}
