#pragma once

#include <Windows.h>

namespace D3dDdi
{
	namespace KernelModeThunks
	{
		RECT getMonitorRect();
		UINT getVsyncCounter();
		void installHooks(HMODULE origDDrawModule);
		void setDcFormatOverride(UINT format);
		void setDcPaletteOverride(bool enable);
		void stopVsyncThread();
		void waitForVsync();
		bool waitForVsyncCounter(UINT counter);
	}
}
