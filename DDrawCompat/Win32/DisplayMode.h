#pragma once

#define WIN32_LEAN_AND_MEAN

#include <Windows.h>

namespace Win32
{
	namespace DisplayMode
	{
		DWORD getBpp();
		ULONG queryDisplaySettingsUniqueness();

		void disableDwm8And16BitMitigation();
		void installHooks();
	}
}
