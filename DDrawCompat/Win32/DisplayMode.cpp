#include <set>
#include <string>
#include <vector>

#include <Common/Comparison.h>
#include <Common/CompatPtr.h>
#include <Common/Hook.h>
#include <Common/ScopedSrwLock.h>
#include <Config/Config.h>
#include <DDraw/DirectDraw.h>
#include <DDraw/ScopedThreadLock.h>
#include <Gdi/Gdi.h>
#include <Gdi/VirtualScreen.h>
#include <Win32/DisplayMode.h>

BOOL WINAPI DWM8And16Bit_IsShimApplied_CallOut() { return FALSE; }
ULONG WINAPI GdiEntry13() { return 0; }
BOOL WINAPI SE_COM_HookInterface(CLSID*, GUID*, DWORD, DWORD) { return 0; }

namespace
{
	using Win32::DisplayMode::DisplayMode;
	using Win32::DisplayMode::EmulatedDisplayMode;

	template <typename Char>
	struct EnumParams
	{
		std::basic_string<Char> deviceName;
		DWORD flags;

		bool operator==(const EnumParams& other) const
		{
			return deviceName == other.deviceName && flags == other.flags;
		}

		bool operator!=(const EnumParams& other) const
		{
			return !(*this == other);
		}
	};

	struct GetMonitorFromDcEnumArgs
	{
		POINT org;
		HMONITOR hmonitor;
	};

	DWORD g_desktopBpp = 0;
	RECT g_cursorRect = {};
	ULONG g_displaySettingsUniquenessBias = 0;
	EmulatedDisplayMode g_emulatedDisplayMode = {};
	Compat::SrwLock g_srwLock;

	BOOL WINAPI dwm8And16BitIsShimAppliedCallOut();
	BOOL WINAPI seComHookInterface(CLSID* clsid, GUID* iid, DWORD unk1, DWORD unk2);

	template <typename DevMode, typename EnumDisplaySettingsExFunc, typename Char>
	SIZE getConfiguredResolution(EnumDisplaySettingsExFunc origEnumDisplaySettingsEx, const Char* deviceName);

	template <typename Char>
	std::wstring getDeviceName(const Char* deviceName);

	HMONITOR getMonitorFromDc(HDC dc);
	MONITORINFO getMonitorInfo(const std::wstring& deviceName);

	template <typename DevMode, typename EnumDisplaySettingsExFunc, typename Char>
	std::vector<DisplayMode> getSupportedDisplayModes(
		EnumDisplaySettingsExFunc origEnumDisplaySettingsEx, const Char* deviceName, DWORD flags);

	void adjustMonitorInfo(MONITORINFO& mi)
	{
		Compat::ScopedSrwLockShared srwLock(g_srwLock);
		if (!g_emulatedDisplayMode.deviceName.empty() &&
			g_emulatedDisplayMode.rect.left == mi.rcMonitor.left &&
			g_emulatedDisplayMode.rect.top == mi.rcMonitor.top)
		{
			mi.rcMonitor.right += g_emulatedDisplayMode.diff.cx;
			mi.rcMonitor.bottom += g_emulatedDisplayMode.diff.cy;
			mi.rcWork.right += g_emulatedDisplayMode.diff.cx;
			mi.rcWork.bottom += g_emulatedDisplayMode.diff.cy;
		}
	}

	template <typename CStr, typename DevMode, typename ChangeDisplaySettingsExFunc, typename EnumDisplaySettingsExFunc>
	LONG changeDisplaySettingsEx(
		ChangeDisplaySettingsExFunc origChangeDisplaySettingsEx,
		EnumDisplaySettingsExFunc origEnumDisplaySettingsEx,
		CStr lpszDeviceName, DevMode* lpDevMode, HWND hwnd, DWORD dwflags, LPVOID lParam)
	{
		DDraw::ScopedThreadLock lock;
		DevMode targetDevMode = {};
		if (lpDevMode)
		{
			targetDevMode = *lpDevMode;
			targetDevMode.dmBitsPerPel = 32;
			SIZE resolutionOverride = getConfiguredResolution<DevMode>(origEnumDisplaySettingsEx, lpszDeviceName);
			if (0 != resolutionOverride.cx)
			{
				targetDevMode.dmPelsWidth = resolutionOverride.cx;
				targetDevMode.dmPelsHeight = resolutionOverride.cy;
			}
		}

		DevMode prevDevMode = {};
		if (!(dwflags & CDS_TEST))
		{
			prevDevMode.dmSize = sizeof(prevDevMode);
			origEnumDisplaySettingsEx(lpszDeviceName, ENUM_CURRENT_SETTINGS, &prevDevMode, 0);
		}

		LONG result = 0;
		if (lpDevMode)
		{
			result = origChangeDisplaySettingsEx(lpszDeviceName, &targetDevMode, hwnd, dwflags, lParam);
			if (DISP_CHANGE_SUCCESSFUL != result &&
				(lpDevMode->dmPelsWidth != targetDevMode.dmPelsWidth || lpDevMode->dmPelsHeight != targetDevMode.dmPelsHeight))
			{
				LOG_ONCE("Failed to apply setting: DisplayResolution = " << targetDevMode.dmPelsWidth << 'x' << targetDevMode.dmPelsHeight);
				targetDevMode.dmPelsWidth = lpDevMode->dmPelsWidth;
				targetDevMode.dmPelsHeight = lpDevMode->dmPelsHeight;
				result = origChangeDisplaySettingsEx(lpszDeviceName, &targetDevMode, hwnd, dwflags, lParam);
			}
		}
		else
		{
			result = origChangeDisplaySettingsEx(lpszDeviceName, nullptr, hwnd, dwflags, lParam);
		}

		if (dwflags & CDS_TEST)
		{
			return result;
		}

		DevMode currDevMode = {};
		currDevMode.dmSize = sizeof(currDevMode);
		origEnumDisplaySettingsEx(lpszDeviceName, ENUM_CURRENT_SETTINGS, &currDevMode, 0);

		if (0 == memcmp(&currDevMode, &prevDevMode, sizeof(currDevMode)))
		{
			HANDLE dwmDxFullScreenTransitionEvent = OpenEventW(
				EVENT_MODIFY_STATE, FALSE, L"DWM_DX_FULLSCREEN_TRANSITION_EVENT");
			SetEvent(dwmDxFullScreenTransitionEvent);
			CloseHandle(dwmDxFullScreenTransitionEvent);
		}

		if (DISP_CHANGE_SUCCESSFUL != result)
		{
			return result;
		}

		RECT clipRect = {};

		{
			Compat::ScopedSrwLockExclusive srwLock(g_srwLock);
			++g_displaySettingsUniquenessBias;
			clipRect = g_cursorRect;
			if (lpDevMode)
			{
				g_emulatedDisplayMode.width = lpDevMode->dmPelsWidth;
				g_emulatedDisplayMode.height = lpDevMode->dmPelsHeight;
				g_emulatedDisplayMode.bpp = lpDevMode->dmBitsPerPel;
				g_emulatedDisplayMode.refreshRate = currDevMode.dmDisplayFrequency;

				g_emulatedDisplayMode.deviceName = getDeviceName(lpszDeviceName);
				g_emulatedDisplayMode.rect = getMonitorInfo(g_emulatedDisplayMode.deviceName).rcMonitor;
				g_emulatedDisplayMode.rect.right = g_emulatedDisplayMode.rect.left + lpDevMode->dmPelsWidth;
				g_emulatedDisplayMode.rect.bottom = g_emulatedDisplayMode.rect.top + lpDevMode->dmPelsHeight;
				g_emulatedDisplayMode.diff.cx = lpDevMode->dmPelsWidth - currDevMode.dmPelsWidth;
				g_emulatedDisplayMode.diff.cy = lpDevMode->dmPelsHeight - currDevMode.dmPelsHeight;

				IntersectRect(&clipRect, &clipRect, &g_emulatedDisplayMode.rect);
			}
			else
			{
				g_emulatedDisplayMode = {};
				g_emulatedDisplayMode.bpp = g_desktopBpp;
			}
		}

		CALL_ORIG_FUNC(ClipCursor)(&clipRect);

		auto& dm = lpDevMode ? *lpDevMode : currDevMode;
		LPARAM resolution = (dm.dmPelsHeight << 16) | dm.dmPelsWidth;
		EnumWindows(sendDisplayChange, resolution);

		SetCursorPos(currDevMode.dmPosition.x + dm.dmPelsWidth / 2, currDevMode.dmPosition.y + dm.dmPelsHeight / 2);
		Gdi::VirtualScreen::update();

		return result;
	}

	LONG WINAPI changeDisplaySettingsExA(
		LPCSTR lpszDeviceName, DEVMODEA* lpDevMode, HWND hwnd, DWORD dwflags, LPVOID lParam)
	{
		LOG_FUNC("ChangeDisplaySettingsExA", lpszDeviceName, lpDevMode, hwnd, dwflags, lParam);
		return LOG_RESULT(changeDisplaySettingsEx(
			CALL_ORIG_FUNC(ChangeDisplaySettingsExA),
			CALL_ORIG_FUNC(EnumDisplaySettingsExA),
			lpszDeviceName, lpDevMode, hwnd, dwflags, lParam));
	}

	LONG WINAPI changeDisplaySettingsExW(
		LPCWSTR lpszDeviceName, DEVMODEW* lpDevMode, HWND hwnd, DWORD dwflags, LPVOID lParam)
	{
		LOG_FUNC("ChangeDisplaySettingsExW", lpszDeviceName, lpDevMode, hwnd, dwflags, lParam);
		return LOG_RESULT(changeDisplaySettingsEx(
			CALL_ORIG_FUNC(ChangeDisplaySettingsExW),
			CALL_ORIG_FUNC(EnumDisplaySettingsExW),
			lpszDeviceName, lpDevMode, hwnd, dwflags, lParam));
	}

	BOOL WINAPI clipCursor(const RECT* lpRect)
	{
		LOG_FUNC("ClipCursor", lpRect);
		BOOL result = CALL_ORIG_FUNC(ClipCursor)(lpRect);
		if (!result)
		{
			return result;
		}

		RECT rect = {};
		CALL_ORIG_FUNC(GetClipCursor)(&rect);

		{
			Compat::ScopedSrwLockExclusive srwLock(g_srwLock);
			g_cursorRect = rect;
			if (!g_emulatedDisplayMode.deviceName.empty())
			{
				IntersectRect(&rect, &rect, &g_emulatedDisplayMode.rect);
				CALL_ORIG_FUNC(ClipCursor)(&rect);
			}
		}

		return result;
	}

	void disableDwm8And16BitMitigation()
	{
		auto user32 = GetModuleHandle("user32");
		Compat::removeShim(user32, "ChangeDisplaySettingsA");
		Compat::removeShim(user32, "ChangeDisplaySettingsW");
		Compat::removeShim(user32, "ChangeDisplaySettingsExA");
		Compat::removeShim(user32, "ChangeDisplaySettingsExW");
		Compat::removeShim(user32, "EnumDisplaySettingsA");
		Compat::removeShim(user32, "EnumDisplaySettingsW");
		Compat::removeShim(user32, "EnumDisplaySettingsExA");
		Compat::removeShim(user32, "EnumDisplaySettingsExW");

		HOOK_FUNCTION(apphelp, DWM8And16Bit_IsShimApplied_CallOut, dwm8And16BitIsShimAppliedCallOut);
		HOOK_FUNCTION(apphelp, SE_COM_HookInterface, seComHookInterface);
	}

	BOOL WINAPI dwm8And16BitIsShimAppliedCallOut()
	{
		LOG_FUNC("DWM8And16Bit_IsShimApplied_CallOut");
		return LOG_RESULT(FALSE);
	}

	template <typename Char, typename DevMode, typename EnumDisplaySettingsExFunc>
	BOOL enumDisplaySettingsEx(EnumDisplaySettingsExFunc origEnumDisplaySettingsEx,
		const Char* lpszDeviceName, DWORD iModeNum, DevMode* lpDevMode, DWORD dwFlags)
	{
		if (ENUM_REGISTRY_SETTINGS == iModeNum || !lpDevMode)
		{
			return origEnumDisplaySettingsEx(lpszDeviceName, iModeNum, lpDevMode, dwFlags);
		}

		if (ENUM_CURRENT_SETTINGS == iModeNum)
		{
			BOOL result = origEnumDisplaySettingsEx(lpszDeviceName, iModeNum, lpDevMode, dwFlags);
			if (result)
			{
				Compat::ScopedSrwLockShared srwLock(g_srwLock);
				if (getDeviceName(lpszDeviceName) == g_emulatedDisplayMode.deviceName)
				{
					lpDevMode->dmBitsPerPel = g_emulatedDisplayMode.bpp;
					lpDevMode->dmPelsWidth = g_emulatedDisplayMode.width;
					lpDevMode->dmPelsHeight = g_emulatedDisplayMode.height;
				}
				else
				{
					lpDevMode->dmBitsPerPel = g_desktopBpp;
				}
			}
			return result;
		}

		thread_local std::vector<DisplayMode> displayModes;
		thread_local EnumParams<Char> lastEnumParams = {};

		EnumParams<Char> currentEnumParams = { lpszDeviceName ? lpszDeviceName : std::basic_string<Char>(), dwFlags };

		if (0 == iModeNum || displayModes.empty() || currentEnumParams != lastEnumParams)
		{
			displayModes = getSupportedDisplayModes<DevMode>(origEnumDisplaySettingsEx, lpszDeviceName, dwFlags);
			lastEnumParams = currentEnumParams;
		}

		if (iModeNum >= displayModes.size() * 3)
		{
			return FALSE;
		}

		const auto& displayMode = displayModes[iModeNum / 3];
		lpDevMode->dmFields = DM_DISPLAYORIENTATION | DM_BITSPERPEL | DM_PELSWIDTH | DM_PELSHEIGHT |
			DM_DISPLAYFLAGS | DM_DISPLAYFREQUENCY;
		lpDevMode->dmDisplayOrientation = DMDO_DEFAULT;
		lpDevMode->dmPelsWidth = displayMode.width;
		lpDevMode->dmPelsHeight = displayMode.height;
		lpDevMode->dmDisplayFlags = 0;
		lpDevMode->dmDisplayFrequency = displayMode.refreshRate;

		switch (iModeNum % 3)
		{
		case 0: lpDevMode->dmBitsPerPel = 8; break;
		case 1: lpDevMode->dmBitsPerPel = 16; break;
		case 2: lpDevMode->dmBitsPerPel = 32; break;
		}

		return TRUE;
	}

	BOOL WINAPI enumDisplaySettingsExA(
		LPCSTR lpszDeviceName, DWORD iModeNum, DEVMODEA* lpDevMode, DWORD dwFlags)
	{
		LOG_FUNC("EnumDisplaySettingsExA", lpszDeviceName, iModeNum, lpDevMode, dwFlags);
		return LOG_RESULT(enumDisplaySettingsEx(CALL_ORIG_FUNC(EnumDisplaySettingsExA),
			lpszDeviceName, iModeNum, lpDevMode, dwFlags));
	}

	BOOL WINAPI enumDisplaySettingsExW(
		LPCWSTR lpszDeviceName, DWORD iModeNum, DEVMODEW* lpDevMode, DWORD dwFlags)
	{
		LOG_FUNC("EnumDisplaySettingsExW", lpszDeviceName, iModeNum, lpDevMode, dwFlags);
		return LOG_RESULT(enumDisplaySettingsEx(CALL_ORIG_FUNC(EnumDisplaySettingsExW),
			lpszDeviceName, iModeNum, lpDevMode, dwFlags));
	}

	ULONG WINAPI gdiEntry13()
	{
		Compat::ScopedSrwLockShared lock(g_srwLock);
		return CALL_ORIG_FUNC(GdiEntry13)() + g_displaySettingsUniquenessBias;
	}

	BOOL WINAPI getClipCursor(LPRECT lpRect)
	{
		LOG_FUNC("GetClipCursor", lpRect);
		BOOL result = CALL_ORIG_FUNC(GetClipCursor)(lpRect);
		if (result)
		{
			*lpRect = g_cursorRect;
		}
		return LOG_RESULT(result);
	}

	template <typename DevMode, typename EnumDisplaySettingsExFunc, typename Char>
	SIZE getConfiguredResolution(EnumDisplaySettingsExFunc origEnumDisplaySettingsEx, const Char* deviceName)
	{
		auto resolution = Config::displayResolution.get();
		if (Config::Settings::DisplayResolution::DESKTOP == resolution)
		{
			DevMode dm = {};
			dm.dmSize = sizeof(dm);
			if (origEnumDisplaySettingsEx(deviceName, ENUM_REGISTRY_SETTINGS, &dm, 0))
			{
				resolution.cx = dm.dmPelsWidth;
				resolution.cy = dm.dmPelsHeight;
			}
			else
			{
				resolution = {};
			}
		}
		return resolution;
	}

	int WINAPI getDeviceCaps(HDC hdc, int nIndex)
	{
		LOG_FUNC("GetDeviceCaps", hdc, nIndex);
		switch (nIndex)
		{
		case BITSPIXEL:
			if (Gdi::isDisplayDc(hdc))
			{
				return LOG_RESULT(Win32::DisplayMode::getBpp());
			}
			break;

		case COLORRES:
			if (8 == Win32::DisplayMode::getBpp() && Gdi::isDisplayDc(hdc))
			{
				return 24;
			}
			break;

		case HORZRES:
		case VERTRES:
			if (Gdi::isDisplayDc(hdc))
			{
				MONITORINFO mi = {};
				mi.cbSize = sizeof(mi);
				GetMonitorInfo(getMonitorFromDc(hdc), &mi);
				if (HORZRES == nIndex)
				{
					return LOG_RESULT(mi.rcMonitor.right - mi.rcMonitor.left);
				}
				else
				{
					return LOG_RESULT(mi.rcMonitor.bottom - mi.rcMonitor.top);
				}
			}
			break;

		case NUMCOLORS:
		case NUMRESERVED:
			if (8 == Win32::DisplayMode::getBpp() && Gdi::isDisplayDc(hdc))
			{
				return 20;
			}
			break;

		case RASTERCAPS:
			if (8 == Win32::DisplayMode::getBpp() && Gdi::isDisplayDc(hdc))
			{
				return LOG_RESULT(CALL_ORIG_FUNC(GetDeviceCaps)(hdc, nIndex) | RC_PALETTE);
			}
			break;

		case SIZEPALETTE:
			if (8 == Win32::DisplayMode::getBpp() && Gdi::isDisplayDc(hdc))
			{
				return 256;
			}
			break;
		}
		return LOG_RESULT(CALL_ORIG_FUNC(GetDeviceCaps)(hdc, nIndex));
	}

	std::size_t getDeviceNameLength(const char* deviceName)
	{
		return std::strlen(deviceName);
	}

	std::size_t getDeviceNameLength(const wchar_t* deviceName)
	{
		return std::wcslen(deviceName);
	}

	template <typename Char>
	std::wstring getDeviceName(const Char* deviceName)
	{
		if (deviceName)
		{
			return std::wstring(deviceName, deviceName + getDeviceNameLength(deviceName));
		}

		MONITORINFOEXW mi = {};
		mi.cbSize = sizeof(mi);
		CALL_ORIG_FUNC(GetMonitorInfoW)(MonitorFromPoint({}, MONITOR_DEFAULTTOPRIMARY), &mi);
		return mi.szDevice;
	}

	BOOL CALLBACK getMonitorFromDcEnum(HMONITOR hMonitor, HDC /*hdcMonitor*/, LPRECT /*lprcMonitor*/, LPARAM dwData)
	{
		auto& args = *reinterpret_cast<GetMonitorFromDcEnumArgs*>(dwData);

		MONITORINFOEX mi = {};
		mi.cbSize = sizeof(mi);
		CALL_ORIG_FUNC(GetMonitorInfoA)(hMonitor, &mi);

		HDC dc = CreateDC(mi.szDevice, nullptr, nullptr, nullptr);
		if (dc)
		{
			POINT org = {};
			GetDCOrgEx(dc, &org);
			DeleteDC(dc);
			if (org == args.org)
			{
				args.hmonitor = hMonitor;
				return FALSE;
			}
		}
		return TRUE;
	}
	
	HMONITOR getMonitorFromDc(HDC dc)
	{
		HWND hwnd = CALL_ORIG_FUNC(WindowFromDC)(dc);
		if (hwnd)
		{
			return MonitorFromWindow(hwnd, MONITOR_DEFAULTTOPRIMARY);
		}
		
		GetMonitorFromDcEnumArgs args = {};
		GetDCOrgEx(dc, &args.org);
		EnumDisplayMonitors(nullptr, nullptr, getMonitorFromDcEnum, reinterpret_cast<LPARAM>(&args));
		return args.hmonitor ? args.hmonitor : MonitorFromPoint({}, MONITOR_DEFAULTTOPRIMARY);
	}

	BOOL CALLBACK getMonitorInfoEnum(HMONITOR hMonitor, HDC /*hdcMonitor*/, LPRECT /*lprcMonitor*/, LPARAM dwData)
	{
		MONITORINFOEXW mi = {};
		mi.cbSize = sizeof(mi);
		CALL_ORIG_FUNC(GetMonitorInfoW)(hMonitor, &mi);
		auto& dest = *reinterpret_cast<MONITORINFOEXW*>(dwData);
		if (0 == wcscmp(mi.szDevice, dest.szDevice))
		{
			dest = mi;
			return FALSE;
		}
		return TRUE;
	}

	MONITORINFO getMonitorInfo(const std::wstring& deviceName)
	{
		MONITORINFOEXW mi = {};
		wcscpy_s(mi.szDevice, deviceName.c_str());
		EnumDisplayMonitors(nullptr, nullptr, &getMonitorInfoEnum, reinterpret_cast<LPARAM>(&mi));
		return mi;
	}

	BOOL WINAPI getMonitorInfoA(HMONITOR hMonitor, LPMONITORINFO lpmi)
	{
		LOG_FUNC("GetMonitorInfoA", hMonitor, lpmi);
		BOOL result = CALL_ORIG_FUNC(GetMonitorInfoA)(hMonitor, lpmi);
		if (result)
		{
			adjustMonitorInfo(*lpmi);
		}
		return LOG_RESULT(result);
	}

	BOOL WINAPI getMonitorInfoW(HMONITOR hMonitor, LPMONITORINFO lpmi)
	{
		LOG_FUNC("GetMonitorInfoW", hMonitor, lpmi);
		BOOL result = CALL_ORIG_FUNC(GetMonitorInfoW)(hMonitor, lpmi);
		if (result)
		{
			adjustMonitorInfo(*lpmi);
		}
		return LOG_RESULT(result);
	}

	template <typename DevMode, typename EnumDisplaySettingsExFunc, typename Char>
	std::vector<DisplayMode> getSupportedDisplayModes(
		EnumDisplaySettingsExFunc origEnumDisplaySettingsEx, const Char* deviceName, DWORD flags)
	{
		std::set<DisplayMode> displayModes;
		DWORD modeNum = 0;
		DevMode dm = {};
		dm.dmSize = sizeof(dm);

		while (origEnumDisplaySettingsEx(deviceName, modeNum, &dm, flags))
		{
			if (32 == dm.dmBitsPerPel)
			{
				displayModes.insert({ dm.dmPelsWidth, dm.dmPelsHeight, dm.dmBitsPerPel, dm.dmDisplayFrequency });
			}
			++modeNum;
		}

		auto resolutionOverride = getConfiguredResolution<DevMode>(origEnumDisplaySettingsEx, deviceName);
		if (0 == resolutionOverride.cx)
		{
			return { displayModes.begin(), displayModes.end() };
		}

		std::set<DWORD> supportedRefreshRates;
		for (const auto& mode : displayModes)
		{
			if (static_cast<LONG>(mode.width) == resolutionOverride.cx &&
				static_cast<LONG>(mode.height) == resolutionOverride.cy)
			{
				supportedRefreshRates.insert(mode.refreshRate);
			}
		}

		if (supportedRefreshRates.empty())
		{
			return { displayModes.begin(), displayModes.end() };
		}

		std::vector<DisplayMode> customDisplayModes;
		DWORD prevWidth = 0;
		DWORD prevHeight = 0;
		for (const auto& mode : displayModes)
		{
			if (mode.width != prevWidth || mode.height != prevHeight)
			{
				for (auto refreshRate : supportedRefreshRates)
				{
					customDisplayModes.push_back({ mode.width, mode.height, mode.bpp, refreshRate });
				}
				prevWidth = mode.width;
				prevHeight = mode.height;
			}
		}
		return customDisplayModes;
	}

	BOOL CALLBACK initMonitor(HMONITOR hMonitor, HDC /*hdcMonitor*/, LPRECT /*lprcMonitor*/, LPARAM /*dwData*/)
	{
		MONITORINFOEX mi = {};
		mi.cbSize = sizeof(mi);
		GetMonitorInfo(hMonitor, &mi);

		DEVMODE dm = {};
		dm.dmSize = sizeof(dm);
		if (EnumDisplaySettings(mi.szDevice, ENUM_CURRENT_SETTINGS, &dm))
		{
			if (32 != dm.dmBitsPerPel)
			{
				dm = {};
				dm.dmSize = sizeof(dm);
				dm.dmFields = DM_BITSPERPEL;
				dm.dmBitsPerPel = 32;
				ChangeDisplaySettingsEx(mi.szDevice, &dm, nullptr, 0, nullptr);
			}
		}

		return TRUE;
	}

	BOOL WINAPI seComHookInterface(CLSID* clsid, GUID* iid, DWORD unk1, DWORD unk2)
	{
		LOG_FUNC("SE_COM_HookInterface", clsid, iid, unk1, unk2);
		if (clsid && (CLSID_DirectDraw == *clsid || CLSID_DirectDraw7 == *clsid))
		{
			return LOG_RESULT(0);
		}
		return LOG_RESULT(CALL_ORIG_FUNC(SE_COM_HookInterface)(clsid, iid, unk1, unk2));
	}

	BOOL CALLBACK sendDisplayChange(HWND hwnd, LPARAM lParam)
	{
		DWORD pid = 0;
		GetWindowThreadProcessId(hwnd, &pid);
		if (GetCurrentProcessId() == pid)
		{
			SendNotifyMessage(hwnd, WM_DISPLAYCHANGE, 0, lParam);
		}
		return TRUE;
	}
}

namespace Win32
{
	namespace DisplayMode
	{
		DWORD getBpp()
		{
			return getEmulatedDisplayMode().bpp;
		}

		EmulatedDisplayMode getEmulatedDisplayMode()
		{
			Compat::ScopedSrwLockShared lock(g_srwLock);
			return g_emulatedDisplayMode;
		}

		ULONG queryDisplaySettingsUniqueness()
		{
			return CALL_ORIG_FUNC(GdiEntry13)();
		}

		void installHooks()
		{
			DEVMODEA dm = {};
			dm.dmSize = sizeof(dm);
			EnumDisplaySettingsEx(nullptr, ENUM_CURRENT_SETTINGS, &dm, 0);
			g_desktopBpp = dm.dmBitsPerPel;
			g_emulatedDisplayMode.bpp = dm.dmBitsPerPel;

			GetClipCursor(&g_cursorRect);
			EnumDisplayMonitors(nullptr, nullptr, &initMonitor, 0);

			HOOK_FUNCTION(user32, ChangeDisplaySettingsExA, changeDisplaySettingsExA);
			HOOK_FUNCTION(user32, ChangeDisplaySettingsExW, changeDisplaySettingsExW);
			HOOK_FUNCTION(user32, ClipCursor, clipCursor);
			HOOK_FUNCTION(user32, EnumDisplaySettingsExA, enumDisplaySettingsExA);
			HOOK_FUNCTION(user32, EnumDisplaySettingsExW, enumDisplaySettingsExW);
			HOOK_FUNCTION(gdi32, GdiEntry13, gdiEntry13);
			HOOK_FUNCTION(user32, GetClipCursor, getClipCursor);
			HOOK_FUNCTION(gdi32, GetDeviceCaps, getDeviceCaps);
			HOOK_FUNCTION(user32, GetMonitorInfoA, getMonitorInfoA);
			HOOK_FUNCTION(user32, GetMonitorInfoW, getMonitorInfoW);

			disableDwm8And16BitMitigation();
		}
	}
}
