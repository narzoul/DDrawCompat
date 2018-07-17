#pragma once

#define CINTERFACE
#define WIN32_LEAN_AND_MEAN

#include <d3d.h>
#include <d3dumddi.h>
#include <../km/d3dkmthk.h>
#include <Windows.h>

#include "D3dDdi/Log/KernelModeThunksLog.h"

static const auto D3DDDI_FLIPINTERVAL_NOOVERRIDE = static_cast<D3DDDI_FLIPINTERVAL_TYPE>(5);

namespace D3dDdi
{
	namespace KernelModeThunks
	{
		HMONITOR getLastOpenAdapterMonitor();
		void installHooks();
		bool isPresentReady();
		void overrideFlipInterval(D3DDDI_FLIPINTERVAL_TYPE flipInterval);
		void releaseVidPnSources();
	}
}
