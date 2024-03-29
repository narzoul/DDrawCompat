#include <map>
#include <set>
#include <string>
#include <vector>

#include <Windows.h>
#include <VersionHelpers.h>

#include <Common/Comparison.h>
#include <Common/CompatPtr.h>
#include <Common/Hook.h>
#include <Common/Log.h>
#include <Common/Rect.h>
#include <Common/ScopedCriticalSection.h>
#include <Config/Settings/DesktopColorDepth.h>
#include <Config/Settings/DesktopResolution.h>
#include <Config/Settings/DisplayRefreshRate.h>
#include <Config/Settings/DisplayResolution.h>
#include <Config/Settings/SupportedResolutions.h>
#include <DDraw/DirectDraw.h>
#include <DDraw/ScopedThreadLock.h>
#include <Gdi/Gdi.h>
#include <Gdi/GuiThread.h>
#include <Gdi/VirtualScreen.h>
#include <Win32/DisplayMode.h>
#include <Win32/DpiAwareness.h>

BOOL WINAPI DWM8And16Bit_IsShimApplied_CallOut() { return FALSE; }
ULONG WINAPI GdiEntry13() { return 0; }
BOOL WINAPI SE_COM_HookInterface(CLSID*, GUID*, DWORD, DWORD) { return 0; }

namespace
{
	using Win32::DisplayMode::DisplayMode;
	using Win32::DisplayMode::EmulatedDisplayMode;

	template <typename Char> struct DevModeType;
	template <> struct DevModeType<CHAR> { typedef DEVMODEA Type; };
	template <> struct DevModeType<WCHAR> { typedef DEVMODEW Type; };
	template <typename Char> using DevMode = typename DevModeType<Char>::Type;

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
	ULONG g_displaySettingsUniquenessBias = 0;
	EmulatedDisplayMode g_emulatedDisplayMode = {};
	ULONG g_monitorInfoUniqueness = 0;
	std::map<HMONITOR, Win32::DisplayMode::MonitorInfo> g_monitorInfo;
	Win32::DisplayMode::MonitorInfo g_emptyMonitorInfo = {};
	Compat::CriticalSection g_cs;

	BOOL WINAPI dwm8And16BitIsShimAppliedCallOut();
	BOOL WINAPI seComHookInterface(CLSID* clsid, GUID* iid, DWORD unk1, DWORD unk2);

	template <typename Char>
	DWORD getConfiguredRefreshRate(const Char* deviceName);

	template <typename Char>
	SIZE getConfiguredResolution(const Char* deviceName);

	template <typename Char>
	std::wstring getDeviceName(const Char* deviceName);

	HMONITOR getMonitorFromDc(HDC dc);

	template <typename Char>
	std::map<SIZE, std::set<DWORD>> getSupportedDisplayModeMap(const Char* deviceName, DWORD flags);
	template <typename Char>
	std::vector<DisplayMode> getSupportedDisplayModes(const Char* deviceName, DWORD flags);

	SIZE makeSize(DWORD width, DWORD height);

	LONG origChangeDisplaySettingsEx(LPCSTR lpszDeviceName, DEVMODEA* lpDevMode, HWND hwnd, DWORD dwflags, LPVOID lParam);
	LONG origChangeDisplaySettingsEx(LPCWSTR lpszDeviceName, DEVMODEW* lpDevMode, HWND hwnd, DWORD dwflags, LPVOID lParam);
	BOOL origEnumDisplaySettingsEx(LPCSTR lpszDeviceName, DWORD iModeNum, DEVMODEA* lpDevMode, DWORD dwFlags);
	BOOL origEnumDisplaySettingsEx(LPCWSTR lpszDeviceName, DWORD iModeNum, DEVMODEW* lpDevMode, DWORD dwFlags);

	void setDwmDxFullscreenTransitionEvent();

	template <typename Char>
	LONG changeDisplaySettingsEx(const Char* lpszDeviceName, typename DevMode<Char>* lpDevMode, HWND hwnd, DWORD dwflags, LPVOID lParam)
	{
		DDraw::ScopedThreadLock ddLock;
		auto desktopResolution = Config::desktopResolution.get();
		if (!lpDevMode && !(dwflags & CDS_TEST) && Config::Settings::DesktopResolution::DESKTOP != desktopResolution)
		{
			auto mi = Win32::DisplayMode::getMonitorInfo(getDeviceName(lpszDeviceName));
			if (0 == mi.rcMonitor.left && 0 == mi.rcMonitor.top)
			{
				DevMode<Char> dm = {};
				dm.dmSize = sizeof(dm);
				enumDisplaySettingsEx(lpszDeviceName, ENUM_REGISTRY_SETTINGS, &dm, 0);

				dm.dmBitsPerPel = g_desktopBpp;
				dm.dmPelsWidth = desktopResolution.cx;
				dm.dmPelsHeight = desktopResolution.cy;

				if (DISP_CHANGE_SUCCESSFUL == changeDisplaySettingsEx(lpszDeviceName, &dm, nullptr, dwflags, nullptr))
				{
					return DISP_CHANGE_SUCCESSFUL;
				}
			}
		}

		DevMode<Char> prevEmulatedDevMode = {};
		prevEmulatedDevMode.dmSize = sizeof(prevEmulatedDevMode);
		enumDisplaySettingsEx(lpszDeviceName, ENUM_CURRENT_SETTINGS, &prevEmulatedDevMode, 0);

		DevMode<Char> targetDevMode = {};
		SIZE emulatedResolution = {};
		if (lpDevMode)
		{
			targetDevMode = *lpDevMode;
			targetDevMode.dmFields |= DM_BITSPERPEL;
			targetDevMode.dmBitsPerPel = 32;
			if (!(targetDevMode.dmFields & DM_PELSWIDTH))
			{
				targetDevMode.dmFields |= DM_PELSWIDTH;
				targetDevMode.dmPelsWidth = prevEmulatedDevMode.dmPelsWidth;
			}
			if (!(targetDevMode.dmFields & DM_PELSHEIGHT))
			{
				targetDevMode.dmFields |= DM_PELSHEIGHT;
				targetDevMode.dmPelsHeight = prevEmulatedDevMode.dmPelsHeight;
			}
			if (!(targetDevMode.dmFields & DM_DISPLAYFREQUENCY))
			{
				targetDevMode.dmFields |= DM_DISPLAYFREQUENCY;
				targetDevMode.dmDisplayFrequency = prevEmulatedDevMode.dmDisplayFrequency;
			}

			emulatedResolution = makeSize(targetDevMode.dmPelsWidth, targetDevMode.dmPelsHeight);
			auto supportedDisplayModeMap(getSupportedDisplayModeMap(lpszDeviceName, 0));
			if (supportedDisplayModeMap.find(emulatedResolution) == supportedDisplayModeMap.end())
			{
				return DISP_CHANGE_BADMODE;
			}

			DevMode<Char> dm = targetDevMode;
			SIZE resolutionOverride = getConfiguredResolution(lpszDeviceName);
			if (0 != resolutionOverride.cx)
			{
				dm.dmPelsWidth = resolutionOverride.cx;
				dm.dmPelsHeight = resolutionOverride.cy;
			}

			DWORD refreshRateOverride = getConfiguredRefreshRate(lpszDeviceName);
			if (0 != refreshRateOverride)
			{
				dm.dmDisplayFrequency = refreshRateOverride;
			}

			if (0 != resolutionOverride.cx || 0 != refreshRateOverride)
			{
				LONG result = origChangeDisplaySettingsEx(lpszDeviceName, &dm, nullptr, CDS_TEST, nullptr);
				if (DISP_CHANGE_SUCCESSFUL == result)
				{
					targetDevMode = dm;
				}
				else
				{
					LOG_ONCE("Failed to apply custom display mode: "
						<< dm.dmPelsWidth << 'x' << dm.dmPelsHeight << '@' << dm.dmDisplayFrequency);
				}
			}
		}

		DevMode<Char> prevDevMode = {};
		if (!(dwflags & CDS_TEST))
		{
			prevDevMode.dmSize = sizeof(prevDevMode);
			origEnumDisplaySettingsEx(lpszDeviceName, ENUM_CURRENT_SETTINGS, &prevDevMode, 0);
		}

		const auto prevDisplaySettingsUniqueness = CALL_ORIG_FUNC(GdiEntry13);
		LONG result = 0;
		if (lpDevMode)
		{
			result = origChangeDisplaySettingsEx(lpszDeviceName, &targetDevMode, hwnd, dwflags, lParam);
		}
		else
		{
			result = origChangeDisplaySettingsEx(lpszDeviceName, nullptr, hwnd, dwflags, lParam);
		}

		if (dwflags & CDS_TEST)
		{
			return result;
		}

		if (CALL_ORIG_FUNC(GdiEntry13) == prevDisplaySettingsUniqueness)
		{
			setDwmDxFullscreenTransitionEvent();
		}

		if (DISP_CHANGE_SUCCESSFUL != result)
		{
			return result;
		}

		DevMode<Char> currDevMode = {};
		currDevMode.dmSize = sizeof(currDevMode);
		origEnumDisplaySettingsEx(lpszDeviceName, ENUM_CURRENT_SETTINGS, &currDevMode, 0);

		{
			Compat::ScopedCriticalSection lock(g_cs);
			if (lpDevMode)
			{
				g_emulatedDisplayMode.width = emulatedResolution.cx;
				g_emulatedDisplayMode.height = emulatedResolution.cy;
				if (lpDevMode->dmFields & DM_BITSPERPEL)
				{
					g_emulatedDisplayMode.bpp = lpDevMode->dmBitsPerPel;
				}
				g_emulatedDisplayMode.refreshRate = targetDevMode.dmDisplayFrequency;
				g_emulatedDisplayMode.deviceName = getDeviceName(lpszDeviceName);
			}
			else
			{
				g_emulatedDisplayMode = {};
				g_emulatedDisplayMode.bpp = g_desktopBpp;
			}

			DevMode<Char> currEmulatedDevMode = {};
			currEmulatedDevMode.dmSize = sizeof(currEmulatedDevMode);
			enumDisplaySettingsEx(lpszDeviceName, ENUM_CURRENT_SETTINGS, &currEmulatedDevMode, 0);

			if (0 != memcmp(&prevEmulatedDevMode, &currEmulatedDevMode, sizeof(currEmulatedDevMode)))
			{
				++g_displaySettingsUniquenessBias;
			}
		}

		const SIZE res = lpDevMode ? emulatedResolution : makeSize(currDevMode.dmPelsWidth, currDevMode.dmPelsHeight);
		if (res.cx != static_cast<LONG>(prevEmulatedDevMode.dmPelsWidth) ||
			res.cy != static_cast<LONG>(prevEmulatedDevMode.dmPelsHeight))
		{
			ClipCursor(nullptr);
			SetCursorPos(currDevMode.dmPosition.x + res.cx / 2, currDevMode.dmPosition.y + res.cy / 2);
			CALL_ORIG_FUNC(EnumWindows)(sendDisplayChange, (res.cy << 16) | res.cx);
		}

		Gdi::VirtualScreen::update();
		return result;
	}

	LONG WINAPI changeDisplaySettingsExA(
		LPCSTR lpszDeviceName, DEVMODEA* lpDevMode, HWND hwnd, DWORD dwflags, LPVOID lParam)
	{
		LOG_FUNC("ChangeDisplaySettingsExA", lpszDeviceName, lpDevMode, hwnd, dwflags, lParam);
		return LOG_RESULT(changeDisplaySettingsEx(lpszDeviceName, lpDevMode, hwnd, dwflags, lParam));
	}

	LONG WINAPI changeDisplaySettingsExW(
		LPCWSTR lpszDeviceName, DEVMODEW* lpDevMode, HWND hwnd, DWORD dwflags, LPVOID lParam)
	{
		LOG_FUNC("ChangeDisplaySettingsExW", lpszDeviceName, lpDevMode, hwnd, dwflags, lParam);
		return LOG_RESULT(changeDisplaySettingsEx(lpszDeviceName, lpDevMode, hwnd, dwflags, lParam));
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

		if (IsWindows8OrGreater())
		{
			HOOK_FUNCTION(apphelp, DWM8And16Bit_IsShimApplied_CallOut, dwm8And16BitIsShimAppliedCallOut);
			HOOK_FUNCTION(apphelp, SE_COM_HookInterface, seComHookInterface);
		}
	}

	BOOL WINAPI dwm8And16BitIsShimAppliedCallOut()
	{
		LOG_FUNC("DWM8And16Bit_IsShimApplied_CallOut");
		return LOG_RESULT(FALSE);
	}

	template <typename Char>
	BOOL enumDisplaySettingsEx(const Char* lpszDeviceName, DWORD iModeNum, DevMode<Char>* lpDevMode, DWORD dwFlags)
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
				Compat::ScopedCriticalSection lock(g_cs);
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
			displayModes = getSupportedDisplayModes(lpszDeviceName, dwFlags);
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
		return LOG_RESULT(enumDisplaySettingsEx(lpszDeviceName, iModeNum, lpDevMode, dwFlags));
	}

	BOOL WINAPI enumDisplaySettingsExW(
		LPCWSTR lpszDeviceName, DWORD iModeNum, DEVMODEW* lpDevMode, DWORD dwFlags)
	{
		LOG_FUNC("EnumDisplaySettingsExW", lpszDeviceName, iModeNum, lpDevMode, dwFlags);
		return LOG_RESULT(enumDisplaySettingsEx(lpszDeviceName, iModeNum, lpDevMode, dwFlags));
	}

	ULONG WINAPI gdiEntry13()
	{
		Compat::ScopedCriticalSection lock(g_cs);
		return CALL_ORIG_FUNC(GdiEntry13)() + g_displaySettingsUniquenessBias;
	}

	template <typename Char>
	DWORD getConfiguredRefreshRate(const Char* deviceName)
	{
		auto refreshRate = Config::displayRefreshRate.get();
		if (Config::Settings::DisplayRefreshRate::DESKTOP == refreshRate)
		{
			DevMode<Char> dm = {};
			dm.dmSize = sizeof(dm);
			if (origEnumDisplaySettingsEx(deviceName, ENUM_REGISTRY_SETTINGS, &dm, 0))
			{
				refreshRate = dm.dmDisplayFrequency;
			}
		}
		return refreshRate;
	}

	template <typename Char>
	SIZE getConfiguredResolution(const Char* deviceName)
	{
		auto resolution = Config::displayResolution.get();
		if (Config::Settings::DisplayResolution::DESKTOP == resolution)
		{
			DevMode<Char> dm = {};
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
				return LOG_RESULT(24);
			}
			break;

		case HORZRES:
		case VERTRES:
			if (Gdi::isDisplayDc(hdc))
			{
				const auto& r = Win32::DisplayMode::getMonitorInfo().rcEmulated;
				return LOG_RESULT(HORZRES == nIndex ? (r.right - r.left) : (r.bottom - r.top));
			}
			break;

		case NUMCOLORS:
		case NUMRESERVED:
			if (8 == Win32::DisplayMode::getBpp() && Gdi::isDisplayDc(hdc))
			{
				return LOG_RESULT(20);
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
				return LOG_RESULT(256);
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

	BOOL WINAPI getMonitorInfoA(HMONITOR hMonitor, LPMONITORINFO lpmi)
	{
		LOG_FUNC("GetMonitorInfoA", hMonitor, lpmi);
		BOOL result = CALL_ORIG_FUNC(GetMonitorInfoA)(hMonitor, lpmi);
		if (result)
		{
			lpmi->rcMonitor = Win32::DisplayMode::getMonitorInfo(hMonitor).rcEmulated;
		}
		return LOG_RESULT(result);
	}

	BOOL WINAPI getMonitorInfoW(HMONITOR hMonitor, LPMONITORINFO lpmi)
	{
		LOG_FUNC("GetMonitorInfoW", hMonitor, lpmi);
		BOOL result = CALL_ORIG_FUNC(GetMonitorInfoW)(hMonitor, lpmi);
		if (result)
		{
			lpmi->rcMonitor = Win32::DisplayMode::getMonitorInfo(hMonitor).rcEmulated;
		}
		return LOG_RESULT(result);
	}

	template <typename Char>
	std::map<SIZE, std::set<DWORD>> getSupportedDisplayModeMap(const Char* deviceName, DWORD flags)
	{
		std::map<SIZE, std::set<DWORD>> nativeDisplayModeMap;

		DWORD modeNum = 0;
		DevMode<Char> dm = {};
		dm.dmSize = sizeof(dm);
		while (origEnumDisplaySettingsEx(deviceName, modeNum, &dm, flags))
		{
			if (32 == dm.dmBitsPerPel)
			{
				nativeDisplayModeMap[makeSize(dm.dmPelsWidth, dm.dmPelsHeight)].insert(dm.dmDisplayFrequency);
			}
			++modeNum;
		}

		const auto& supportedResolutions = Config::supportedResolutions.get();
		std::map<SIZE, std::set<DWORD>> displayModeMap;
		if (supportedResolutions.find(Config::Settings::SupportedResolutions::NATIVE) != supportedResolutions.end())
		{
			displayModeMap = nativeDisplayModeMap;
		}

		const auto resolutionOverride = getConfiguredResolution(deviceName);
		const auto it = nativeDisplayModeMap.find({ resolutionOverride.cx, resolutionOverride.cy });
		if (it != nativeDisplayModeMap.end())
		{
			for (auto& v : displayModeMap)
			{
				v.second = it->second;
			}
		}

		for (auto& v : supportedResolutions)
		{
			if (v != Config::Settings::SupportedResolutions::NATIVE)
			{
				if (it != nativeDisplayModeMap.end())
				{
					displayModeMap[{ v.cx, v.cy }] = it->second;
				}
				else
				{
					auto iter = nativeDisplayModeMap.find({ v.cx, v.cy });
					if (iter != nativeDisplayModeMap.end())
					{
						displayModeMap.insert(*iter);
					}
				}
			}
		}

		return displayModeMap;
	}

	template <typename Char>
	std::vector<DisplayMode> getSupportedDisplayModes(const Char* deviceName, DWORD flags)
	{
		auto displayModeMap(getSupportedDisplayModeMap(deviceName, flags));
		std::vector<DisplayMode> displayModeVector;
		for (const auto& v : displayModeMap)
		{
			for (const auto& r : v.second)
			{
				displayModeVector.push_back({ static_cast<DWORD>(v.first.cx), static_cast<DWORD>(v.first.cy), 32, r });
			}
		}
		return displayModeVector;
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

	SIZE makeSize(DWORD width, DWORD height)
	{
		return { static_cast<LONG>(width), static_cast<LONG>(height) };
	}

	LONG origChangeDisplaySettingsEx(LPCSTR lpszDeviceName, DEVMODEA* lpDevMode, HWND hwnd, DWORD dwflags, LPVOID lParam)
	{
		return CALL_ORIG_FUNC(ChangeDisplaySettingsExA)(lpszDeviceName, lpDevMode, hwnd, dwflags, lParam);
	}

	LONG origChangeDisplaySettingsEx(LPCWSTR lpszDeviceName, DEVMODEW* lpDevMode, HWND hwnd, DWORD dwflags, LPVOID lParam)
	{
		return CALL_ORIG_FUNC(ChangeDisplaySettingsExW)(lpszDeviceName, lpDevMode, hwnd, dwflags, lParam);
	}

	BOOL origEnumDisplaySettingsEx(LPCSTR lpszDeviceName, DWORD iModeNum, DEVMODEA* lpDevMode, DWORD dwFlags)
	{
		return CALL_ORIG_FUNC(EnumDisplaySettingsExA)(lpszDeviceName, iModeNum, lpDevMode, dwFlags);
	}

	BOOL origEnumDisplaySettingsEx(LPCWSTR lpszDeviceName, DWORD iModeNum, DEVMODEW* lpDevMode, DWORD dwFlags)
	{
		return CALL_ORIG_FUNC(EnumDisplaySettingsExW)(lpszDeviceName, iModeNum, lpDevMode, dwFlags);
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

	void setDwmDxFullscreenTransitionEvent()
	{
		HANDLE dwmDxFullscreenTransitionEvent = OpenEventW(
			EVENT_MODIFY_STATE, FALSE, L"DWM_DX_FULLSCREEN_TRANSITION_EVENT");
		if (dwmDxFullscreenTransitionEvent)
		{
			SetEvent(dwmDxFullscreenTransitionEvent);
			CloseHandle(dwmDxFullscreenTransitionEvent);
		}
	}

	BOOL CALLBACK updateMonitorInfoEnum(HMONITOR hMonitor, HDC /*hdcMonitor*/, LPRECT /*lprcMonitor*/, LPARAM /*dwData*/)
	{
		auto& mi = g_monitorInfo[hMonitor];
		mi.cbSize = sizeof(MONITORINFOEXW);
		CALL_ORIG_FUNC(GetMonitorInfoW)(hMonitor, &mi);

		DEVMODEW dm = {};
		dm.dmSize = sizeof(dm);
		CALL_ORIG_FUNC(EnumDisplaySettingsExW)(mi.szDevice, ENUM_CURRENT_SETTINGS, &dm, 0);
		mi.rcReal.left = dm.dmPosition.x;
		mi.rcReal.top = dm.dmPosition.y;
		mi.rcReal.right = dm.dmPosition.x + dm.dmPelsWidth;
		mi.rcReal.bottom = dm.dmPosition.y + dm.dmPelsHeight;

		mi.rcDpiAware = Win32::DpiAwareness::isMixedModeSupported() ? mi.rcReal : mi.rcMonitor;

		mi.rcEmulated = mi.rcMonitor;
		if (g_emulatedDisplayMode.deviceName == mi.szDevice)
		{
			mi.rcEmulated.right = mi.rcEmulated.left + g_emulatedDisplayMode.width;
			mi.rcEmulated.bottom = mi.rcEmulated.top + g_emulatedDisplayMode.height;
			mi.bpp = g_emulatedDisplayMode.bpp;
			mi.isEmulated = true;
		}
		else
		{
			mi.bpp = g_desktopBpp;
		}

		mi.dpiScale = MulDiv(100, mi.rcReal.right - mi.rcReal.left, mi.rcMonitor.right - mi.rcMonitor.left);

		if (0 == mi.rcMonitor.left && 0 == mi.rcMonitor.top)
		{
			g_monitorInfo[nullptr] = mi;
		}

		LOG_DEBUG << "updateMonitorInfoEnum: " << hMonitor << " " << mi;
		return TRUE;
	}

	void updateMonitorInfo()
	{
		const auto uniqueness = gdiEntry13();
		if (uniqueness != g_monitorInfoUniqueness || g_monitorInfo.empty())
		{
			g_monitorInfo.clear();
			g_monitorInfoUniqueness = uniqueness;
			EnumDisplayMonitors(nullptr, nullptr, &updateMonitorInfoEnum, 0);
		}
	}
}

namespace Win32
{
	namespace DisplayMode
	{
		std::ostream& operator<<(std::ostream& os, const MonitorInfo& mi)
		{
			return Compat::LogStruct(os)
				<< mi.rcMonitor
				<< mi.rcWork
				<< Compat::hex(mi.dwFlags)
				<< mi.szDevice
				<< mi.rcReal
				<< mi.rcDpiAware
				<< mi.rcEmulated
				<< mi.bpp
				<< mi.dpiScale
				<< mi.isEmulated;
		}

		std::map<HMONITOR, MonitorInfo> getAllMonitorInfo()
		{
			Compat::ScopedCriticalSection lock(g_cs);
			updateMonitorInfo();
			auto mi = g_monitorInfo;
			mi.erase(nullptr);
			return mi;
		}

		DWORD getBpp()
		{
			Compat::ScopedCriticalSection lock(g_cs);
			return g_emulatedDisplayMode.bpp;
		}

		EmulatedDisplayMode getEmulatedDisplayMode()
		{
			Compat::ScopedCriticalSection lock(g_cs);
			return g_emulatedDisplayMode;
		}

		const MonitorInfo& getMonitorInfo(HMONITOR monitor)
		{
			Compat::ScopedCriticalSection lock(g_cs);
			updateMonitorInfo();
			auto it = g_monitorInfo.find(monitor);
			return it != g_monitorInfo.end() ? it->second : g_emptyMonitorInfo;
		}

		const MonitorInfo& getMonitorInfo(HWND hwnd)
		{
			return getMonitorInfo(hwnd ? MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST) : nullptr);
		}

		const MonitorInfo& getMonitorInfo(POINT pt)
		{
			return getMonitorInfo(MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST));
		}

		const MonitorInfo& getMonitorInfo(const std::wstring& deviceName)
		{
			Compat::ScopedCriticalSection lock(g_cs);
			updateMonitorInfo();
			for (const auto& mi : g_monitorInfo)
			{
				if (deviceName == mi.second.szDevice)
				{
					return mi.second;
				}
			}
			return g_emptyMonitorInfo;
		}

		ULONG queryDisplaySettingsUniqueness()
		{
			return CALL_ORIG_FUNC(GdiEntry13)();
		}

		ULONG queryEmulatedDisplaySettingsUniqueness()
		{
			return gdiEntry13();
		}

		void installHooks()
		{
			g_desktopBpp = Config::desktopColorDepth.get();
			if (Config::Settings::DesktopColorDepth::INITIAL == g_desktopBpp)
			{
				DEVMODEA dm = {};
				dm.dmSize = sizeof(dm);
				EnumDisplaySettingsEx(nullptr, ENUM_CURRENT_SETTINGS, &dm, 0);
				g_desktopBpp = dm.dmBitsPerPel;
			}
			g_emulatedDisplayMode.bpp = g_desktopBpp;

			EnumDisplayMonitors(nullptr, nullptr, &initMonitor, 0);

			HOOK_FUNCTION(user32, ChangeDisplaySettingsExA, changeDisplaySettingsExA);
			HOOK_FUNCTION(user32, ChangeDisplaySettingsExW, changeDisplaySettingsExW);
			HOOK_FUNCTION(user32, EnumDisplaySettingsExA, enumDisplaySettingsExA);
			HOOK_FUNCTION(user32, EnumDisplaySettingsExW, enumDisplaySettingsExW);
			HOOK_FUNCTION(gdi32, GdiEntry13, gdiEntry13);
			HOOK_FUNCTION(gdi32, GetDeviceCaps, getDeviceCaps);
			HOOK_FUNCTION(user32, GetMonitorInfoA, getMonitorInfoA);
			HOOK_FUNCTION(user32, GetMonitorInfoW, getMonitorInfoW);

			disableDwm8And16BitMitigation();

			if (Config::Settings::DesktopResolution::DESKTOP != Config::desktopResolution.get())
			{
				changeDisplaySettingsExA(nullptr, nullptr, nullptr, 0, nullptr);
			}
		}
	}
}
