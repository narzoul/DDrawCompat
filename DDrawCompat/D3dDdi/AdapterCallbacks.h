#pragma once

#include <d3d.h>
#include <d3dumddi.h>

#include <D3dDdi/Log/AdapterCallbacksLog.h>

namespace D3dDdi
{
	namespace AdapterCallbacks
	{
		void hookVtable(const D3DDDI_ADAPTERCALLBACKS& vtable, UINT version);
	}
}
