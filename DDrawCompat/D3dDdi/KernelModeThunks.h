#pragma once

#include <ddraw.h>

#include <Common/CompatRef.h>

namespace D3dDdi
{
	namespace KernelModeThunks
	{
		struct AdapterInfo
		{
			UINT adapter;
			UINT vidPnSourceId;
			LUID luid;
			RECT monitorRect;
		};

		AdapterInfo getAdapterInfo(CompatRef<IDirectDraw7> dd);
		AdapterInfo getLastOpenAdapterInfo();
		RECT getMonitorRect();
		long long getQpcLastVsync();
		UINT getVsyncCounter();
		void installHooks();
		void setDcFormatOverride(UINT format);
		void setDcPaletteOverride(bool enable);
		void waitForVsync();
		bool waitForVsyncCounter(UINT counter);
	}
}
