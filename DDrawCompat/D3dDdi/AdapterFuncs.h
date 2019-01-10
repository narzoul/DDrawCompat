#pragma once

#include <d3d.h>
#include <d3dumddi.h>
#include <d3dnthal.h>

#include "D3dDdi/D3dDdiVtable.h"
#include "D3dDdi/Log/AdapterFuncsLog.h"
#include "D3dDdi/Visitors/AdapterFuncsVisitor.h"

namespace D3dDdi
{
	class AdapterFuncs : public D3dDdiVtable<D3DDDI_ADAPTERFUNCS>
	{
	public:
		static const D3DNTHAL_D3DEXTENDEDCAPS& getD3dExtendedCaps(HANDLE adapter);
		static void onOpenAdapter(HMODULE module, HANDLE adapter);
		static void setCompatVtable(D3DDDI_ADAPTERFUNCS& vtable);
	};
}

SET_COMPAT_VTABLE(D3DDDI_ADAPTERFUNCS, D3dDdi::AdapterFuncs);
