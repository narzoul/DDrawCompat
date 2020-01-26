#pragma once

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
		void setDcPaletteOverride(bool enable);
		void waitForVerticalBlank();
	}
}
