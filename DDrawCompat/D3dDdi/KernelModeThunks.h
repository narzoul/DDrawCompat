#pragma once

#include <Windows.h>
#include <winternl.h>
#include <d3dkmthk.h>

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

		void fixPresent(D3DKMT_PRESENT& data);
		AdapterInfo getAdapterInfo(CompatRef<IDirectDraw7> dd);
		AdapterInfo getLastOpenAdapterInfo();
		long long getQpcLastVsync();
		UINT getVsyncCounter();
		void installHooks();
		void setDcFormatOverride(UINT format);
		void setDcPaletteOverride(PALETTEENTRY* palette);
		bool waitForVsyncCounter(UINT counter);
	}
}
