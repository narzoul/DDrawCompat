#pragma once

#include <d3d.h>
#include <d3dumddi.h>

#include "D3dDdi/D3dDdiVtable.h"
#include "D3dDdi/Log/DeviceFuncsLog.h"
#include "D3dDdi/Visitors/DeviceFuncsVisitor.h"

namespace D3dDdi
{
	class DeviceFuncs : public D3dDdiVtable<D3DDDI_DEVICEFUNCS>
	{
	public:
		static void onCreateDevice(HANDLE adapter, HANDLE device);
		static void setCompatVtable(D3DDDI_DEVICEFUNCS& vtable);
	};
}

SET_COMPAT_VTABLE(D3DDDI_DEVICEFUNCS, D3dDdi::DeviceFuncs);
