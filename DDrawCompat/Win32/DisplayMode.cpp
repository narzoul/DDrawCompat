#include <string>
#include <vector>

#include "Common/CompatPtr.h"
#include "Common/Hook.h"
#include "DDraw/DirectDraw.h"
#include "DDraw/ScopedThreadLock.h"
#include "Gdi/Gdi.h"
#include "Win32/DisplayMode.h"

BOOL WINAPI DWM8And16Bit_IsShimApplied_CallOut() { return FALSE; };

namespace
{
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

	DWORD g_origBpp = 0;
	DWORD g_currentBpp = 0;
	DWORD g_lastBpp = 0;
	DWORD g_ddrawBpp = 0;

	BOOL WINAPI enumDisplaySettingsExA(
		LPCSTR lpszDeviceName, DWORD iModeNum, DEVMODEA* lpDevMode, DWORD dwFlags);
	BOOL WINAPI enumDisplaySettingsExW(
		LPCWSTR lpszDeviceName, DWORD iModeNum, DEVMODEW* lpDevMode, DWORD dwFlags);

	template <typename CStr, typename DevMode, typename ChangeDisplaySettingsExFunc,
		typename EnumDisplaySettingsExFunc>
	LONG changeDisplaySettingsEx(
		ChangeDisplaySettingsExFunc origChangeDisplaySettingsEx,
		EnumDisplaySettingsExFunc origEnumDisplaySettingsEx,
		CStr lpszDeviceName, DevMode* lpDevMode, HWND hwnd, DWORD dwflags, LPVOID lParam)
	{
		DDraw::ScopedThreadLock lock;
		DevMode prevDevMode = {};
		if (!(dwflags & CDS_TEST))
		{
			prevDevMode.dmSize = sizeof(prevDevMode);
			origEnumDisplaySettingsEx(lpszDeviceName, ENUM_CURRENT_SETTINGS, &prevDevMode, 0);
		}

		BOOL result = FALSE;
		if (lpDevMode)
		{
			DWORD origBpp = lpDevMode->dmBitsPerPel;
			lpDevMode->dmBitsPerPel = 32;
			result = origChangeDisplaySettingsEx(lpszDeviceName, lpDevMode, hwnd, dwflags, lParam);
			lpDevMode->dmBitsPerPel = origBpp;
		}
		else
		{
			result = origChangeDisplaySettingsEx(lpszDeviceName, lpDevMode, hwnd, dwflags, lParam);
		}

		if (SUCCEEDED(result) && !(dwflags & CDS_TEST))
		{
			if (lpDevMode)
			{
				g_currentBpp = lpDevMode->dmBitsPerPel;
				g_lastBpp = lpDevMode->dmBitsPerPel;
			}
			else
			{
				g_currentBpp = g_origBpp;
			}

			DevMode currDevMode = {};
			currDevMode.dmSize = sizeof(currDevMode);
			origEnumDisplaySettingsEx(lpszDeviceName, ENUM_CURRENT_SETTINGS, &currDevMode, 0);

			if (currDevMode.dmPelsWidth == prevDevMode.dmPelsWidth &&
				currDevMode.dmPelsHeight == prevDevMode.dmPelsHeight &&
				currDevMode.dmBitsPerPel == prevDevMode.dmBitsPerPel &&
				currDevMode.dmDisplayFrequency == prevDevMode.dmDisplayFrequency &&
				currDevMode.dmDisplayFlags == prevDevMode.dmDisplayFlags)
			{
				HANDLE dwmDxFullScreenTransitionEvent = OpenEventW(
					EVENT_MODIFY_STATE, FALSE, L"DWM_DX_FULLSCREEN_TRANSITION_EVENT");
				SetEvent(dwmDxFullScreenTransitionEvent);
				CloseHandle(dwmDxFullScreenTransitionEvent);
			}
		}

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

	template <typename CStr, typename DevMode, typename ChangeDisplaySettingsExFunc>
	LONG ddrawChangeDisplaySettingsEx(
		ChangeDisplaySettingsExFunc changeDisplaySettingsEx,
		CStr lpszDeviceName, DevMode* lpDevMode, HWND hwnd, DWORD dwflags, LPVOID lParam)
	{
		if (lpDevMode && 0 != lpDevMode->dmBitsPerPel)
		{
			lpDevMode->dmBitsPerPel = (0 != g_ddrawBpp) ? g_ddrawBpp : g_lastBpp;
		}
		return changeDisplaySettingsEx(lpszDeviceName, lpDevMode, hwnd, dwflags, lParam);
	}

	LONG WINAPI ddrawChangeDisplaySettingsA(
		DEVMODEA* lpDevMode, DWORD dwflags)
	{
		return ddrawChangeDisplaySettingsEx(&changeDisplaySettingsExA,
			nullptr, lpDevMode, nullptr, dwflags, nullptr);
	}

	LONG WINAPI ddrawChangeDisplaySettingsW(
		DEVMODEW* lpDevMode, DWORD dwflags)
	{
		return ddrawChangeDisplaySettingsEx(&changeDisplaySettingsExW,
			nullptr, lpDevMode, nullptr, dwflags, nullptr);
	}

	LONG WINAPI ddrawChangeDisplaySettingsExA(
		LPCSTR lpszDeviceName, DEVMODEA* lpDevMode, HWND hwnd, DWORD dwflags, LPVOID lParam)
	{
		return ddrawChangeDisplaySettingsEx(&changeDisplaySettingsExA,
			lpszDeviceName, lpDevMode, hwnd, dwflags, lParam);
	}

	LONG WINAPI ddrawChangeDisplaySettingsExW(
		LPCWSTR lpszDeviceName, DEVMODEW* lpDevMode, HWND hwnd, DWORD dwflags, LPVOID lParam)
	{
		return ddrawChangeDisplaySettingsEx(&changeDisplaySettingsExW,
			lpszDeviceName, lpDevMode, hwnd, dwflags, lParam);
	}

	template <typename CStr, typename DevMode, typename EnumDisplaySettingsExFunc>
	BOOL WINAPI ddrawEnumDisplaySettingsEx(
		EnumDisplaySettingsExFunc origEnumDisplaySettingsEx,
		EnumDisplaySettingsExFunc enumDisplaySettingsEx,
		CStr lpszDeviceName, DWORD iModeNum, DevMode* lpDevMode, DWORD dwFlags)
	{
		if (ENUM_CURRENT_SETTINGS == iModeNum)
		{
			return origEnumDisplaySettingsEx(lpszDeviceName, iModeNum, lpDevMode, dwFlags);
		}
		return enumDisplaySettingsEx(lpszDeviceName, iModeNum, lpDevMode, dwFlags);
	}

	BOOL WINAPI ddrawEnumDisplaySettingsA(LPCSTR lpszDeviceName, DWORD iModeNum, DEVMODEA* lpDevMode)
	{
		return ddrawEnumDisplaySettingsEx(CALL_ORIG_FUNC(EnumDisplaySettingsExA), &enumDisplaySettingsExA,
			lpszDeviceName, iModeNum, lpDevMode, 0);
	}

	BOOL WINAPI ddrawEnumDisplaySettingsW(LPCWSTR lpszDeviceName, DWORD iModeNum, DEVMODEW* lpDevMode)
	{
		return ddrawEnumDisplaySettingsEx(CALL_ORIG_FUNC(EnumDisplaySettingsExW), &enumDisplaySettingsExW,
			lpszDeviceName, iModeNum, lpDevMode, 0);
	}

	BOOL WINAPI ddrawEnumDisplaySettingsExA(
		LPCSTR lpszDeviceName, DWORD iModeNum, DEVMODEA* lpDevMode, DWORD dwFlags)
	{
		return ddrawEnumDisplaySettingsEx(CALL_ORIG_FUNC(EnumDisplaySettingsExA), &enumDisplaySettingsExA,
			lpszDeviceName, iModeNum, lpDevMode, dwFlags);
	}

	BOOL WINAPI ddrawEnumDisplaySettingsExW(
		LPCWSTR lpszDeviceName, DWORD iModeNum, DEVMODEW* lpDevMode, DWORD dwFlags)
	{
		return ddrawEnumDisplaySettingsEx(CALL_ORIG_FUNC(EnumDisplaySettingsExW), &enumDisplaySettingsExW,
			lpszDeviceName, iModeNum, lpDevMode, dwFlags);
	}

	BOOL WINAPI dwm8And16BitIsShimAppliedCallOut()
	{
		return FALSE;
	}

	template <typename Char, typename DevMode, typename EnumDisplaySettingsExFunc>
	BOOL enumDisplaySettingsEx(EnumDisplaySettingsExFunc origEnumDisplaySettingsEx,
		const Char* lpszDeviceName, DWORD iModeNum, DevMode* lpDevMode, DWORD dwFlags)
	{
		if (ENUM_REGISTRY_SETTINGS == iModeNum || !lpDevMode)
		{
			BOOL result = origEnumDisplaySettingsEx(lpszDeviceName, iModeNum, lpDevMode, dwFlags);
			return result;
		}

		if (ENUM_CURRENT_SETTINGS == iModeNum)
		{
			BOOL result = origEnumDisplaySettingsEx(lpszDeviceName, iModeNum, lpDevMode, dwFlags);
			if (result)
			{
				lpDevMode->dmBitsPerPel = g_currentBpp;
			}
			return result;
		}

		thread_local std::vector<DevMode> devModes;
		thread_local EnumParams<Char> lastEnumParams = {};

		EnumParams<Char> currentEnumParams = {
			lpszDeviceName ? lpszDeviceName : std::basic_string<Char>(), dwFlags };

		if (0 == iModeNum || devModes.empty() || currentEnumParams != lastEnumParams)
		{
			devModes.clear();
			lastEnumParams = currentEnumParams;

			DWORD modeNum = 0;
			DevMode dm = {};
			dm.dmSize = sizeof(dm);
			while (origEnumDisplaySettingsEx(lpszDeviceName, modeNum, &dm, dwFlags))
			{
				if (32 == dm.dmBitsPerPel)
				{
					dm.dmBitsPerPel = 8;
					devModes.push_back(dm);
					dm.dmBitsPerPel = 16;
					devModes.push_back(dm);
					dm.dmBitsPerPel = 32;
					devModes.push_back(dm);
				}
				++modeNum;
			}
		}

		if (iModeNum >= devModes.size())
		{
			return FALSE;
		}

		*lpDevMode = devModes[iModeNum];
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

	int WINAPI getDeviceCaps(HDC hdc, int nIndex)
	{
		LOG_FUNC("GetDeviceCaps", hdc, nIndex);
		if (BITSPIXEL == nIndex && Gdi::isDisplayDc(hdc))
		{
			return LOG_RESULT(g_currentBpp);
		}
		return LOG_RESULT(CALL_ORIG_FUNC(GetDeviceCaps)(hdc, nIndex));
	}
}

namespace Win32
{
	namespace DisplayMode
	{
		DWORD getBpp()
		{
			return g_currentBpp;
		}

		ULONG queryDisplaySettingsUniqueness()
		{
			static auto ddQueryDisplaySettingsUniqueness = reinterpret_cast<ULONG(APIENTRY*)()>(
				GetProcAddress(GetModuleHandle("gdi32"), "GdiEntry13"));
			return ddQueryDisplaySettingsUniqueness();
		}

		void setDDrawBpp(DWORD bpp)
		{
			g_ddrawBpp = bpp;
		}

		void disableDwm8And16BitMitigation()
		{
			HOOK_FUNCTION(apphelp, DWM8And16Bit_IsShimApplied_CallOut, dwm8And16BitIsShimAppliedCallOut);
		}

		void installHooks(HMODULE origDDrawModule)
		{
			DEVMODEA devMode = {};
			devMode.dmSize = sizeof(devMode);
			EnumDisplaySettingsEx(nullptr, ENUM_CURRENT_SETTINGS, &devMode, 0);
			g_origBpp = devMode.dmBitsPerPel;
			g_currentBpp = g_origBpp;
			g_lastBpp = g_origBpp;

			if (32 != devMode.dmBitsPerPel)
			{
				devMode.dmBitsPerPel = 32;
				ChangeDisplaySettings(&devMode, 0);
			}

			HOOK_FUNCTION(user32, ChangeDisplaySettingsExA, changeDisplaySettingsExA);
			HOOK_FUNCTION(user32, ChangeDisplaySettingsExW, changeDisplaySettingsExW);
			HOOK_FUNCTION(user32, EnumDisplaySettingsExA, enumDisplaySettingsExA);
			HOOK_FUNCTION(user32, EnumDisplaySettingsExW, enumDisplaySettingsExW);
			HOOK_FUNCTION(gdi32, GetDeviceCaps, getDeviceCaps);

			Compat::hookIatFunction(origDDrawModule, "user32.dll", "ChangeDisplaySettingsA",
				&ddrawChangeDisplaySettingsA);
			Compat::hookIatFunction(origDDrawModule, "user32.dll", "ChangeDisplaySettingsW",
				&ddrawChangeDisplaySettingsW);
			Compat::hookIatFunction(origDDrawModule, "user32.dll", "ChangeDisplaySettingsExA",
				&ddrawChangeDisplaySettingsExA);
			Compat::hookIatFunction(origDDrawModule, "user32.dll", "ChangeDisplaySettingsExW",
				&ddrawChangeDisplaySettingsExW);
			Compat::hookIatFunction(origDDrawModule, "user32.dll", "EnumDisplaySettingsA",
				&ddrawEnumDisplaySettingsA);
			Compat::hookIatFunction(origDDrawModule, "user32.dll", "EnumDisplaySettingsW",
				&ddrawEnumDisplaySettingsW);
			Compat::hookIatFunction(origDDrawModule, "user32.dll", "EnumDisplaySettingsExA",
				&ddrawEnumDisplaySettingsExA);
			Compat::hookIatFunction(origDDrawModule, "user32.dll", "EnumDisplaySettingsExW",
				&ddrawEnumDisplaySettingsExW);
		}
	}
}
