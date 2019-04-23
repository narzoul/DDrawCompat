#pragma once

#define WIN32_LEAN_AND_MEAN

#include <Windows.h>

namespace D3dDdi
{
	namespace KernelModeThunks
	{
		UINT getLastFlipInterval();
		UINT getLastDisplayedFrameCount();
		UINT getLastSubmittedFrameCount();
		RECT getMonitorRect();
		long long getQpcLastVerticalBlank();
		void installHooks(HMODULE origDDrawModule);
		void setFlipIntervalOverride(UINT flipInterval);
		void setDcFormatOverride(UINT format);
		void waitForVerticalBlank();
	}
}
