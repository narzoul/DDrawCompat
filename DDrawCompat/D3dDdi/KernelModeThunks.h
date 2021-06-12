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
			MONITORINFOEXW monitorInfo;
		};

		AdapterInfo getAdapterInfo(CompatRef<IDirectDraw7> dd);
		AdapterInfo getLastOpenAdapterInfo();
		long long getQpcLastVsync();
		UINT getVsyncCounter();
		void installHooks();
		void setDcFormatOverride(UINT format);
		void setDcPaletteOverride(bool enable);
		void waitForVsync();
		bool waitForVsyncCounter(UINT counter);
	}
}
