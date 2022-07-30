#pragma once

#include <tuple>

#include <Windows.h>

#include <Common/Comparison.h>

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
			RECT rect;
			SIZE diff;
		};

		DWORD getBpp();
		EmulatedDisplayMode getEmulatedDisplayMode();
		MONITORINFOEXW getMonitorInfo(const std::wstring& deviceName);
		ULONG queryDisplaySettingsUniqueness();

		void installHooks();

		using ::operator<;

		inline auto toTuple(const DisplayMode& dm)
		{
			return std::make_tuple(dm.width, dm.height, dm.bpp, dm.refreshRate);
		}
	}
}
