#pragma once

#include "CompatVtable.h"
#include "D3dDdiDeviceFuncsVisitor.h"

class CompatD3dDdiDeviceFuncs : public CompatVtable<CompatD3dDdiDeviceFuncs, D3dDdiDeviceFuncsIntf>
{
public:
	static void setCompatVtable(D3DDDI_DEVICEFUNCS& vtable);
};
