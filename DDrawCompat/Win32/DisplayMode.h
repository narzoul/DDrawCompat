#pragma once

#include <map>

#include <Windows.h>

#include <Common/Comparison.h>
#include <Gdi/Region.h>

namespace Win32
{
	namespace DisplayMode
	{
		struct DisplayMode
		{
			DWORD width;
			DWORD height;
			DWORD bpp;
			DWORD refreshRate;
		};

		struct EmulatedDisplayMode : DisplayMode
		{
			std::wstring deviceName;
		};

		struct MonitorInfo : MONITORINFOEXW
		{
			RECT rcReal;
			RECT rcDpiAware;
			RECT rcEmulated;
			DWORD bpp;
			DWORD dpiScale;
			DWORD realDpiScale;
			bool isEmulated;
		};

		std::ostream& operator<<(std::ostream& os, const MonitorInfo& mi);

		std::map<HMONITOR, MonitorInfo> getAllMonitorInfo();
		DWORD getBpp();
		const MonitorInfo& getDesktopMonitorInfo();
		EmulatedDisplayMode getEmulatedDisplayMode();
		const MonitorInfo& getMonitorInfo(HMONITOR monitor = nullptr);
		const MonitorInfo& getMonitorInfo(HWND hwnd);
		const MonitorInfo& getMonitorInfo(POINT pt);
		const MonitorInfo& getMonitorInfo(const std::wstring& deviceName);
		RECT getRealBounds();
		ULONG queryDisplaySettingsUniqueness();
		ULONG queryEmulatedDisplaySettingsUniqueness();

		void installHooks();
	}
}
