#pragma once

#include <string>

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
			std::wstring deviceName;
		};

		void enableWaitForGammaRamp(bool enable);
		void fixPresent(D3DKMT_PRESENT& data);
		AdapterInfo getAdapterInfo(CompatRef<IDirectDraw7> dd);
		AdapterInfo getLastOpenAdapterInfo();
		int getVsyncCounter();
		void installHooks();
		bool isFlipPending();
		bool isPresentPending();
		void setDcFormatOverride(UINT format);
		void setDcPaletteOverride(PALETTEENTRY* palette);
		void setFlipEndVsyncCount();
		void setPresentEndVsyncCount();
		void waitForFlipEnd();
		void waitForPresentEnd();
	}
}
