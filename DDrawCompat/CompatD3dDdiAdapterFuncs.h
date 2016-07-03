#pragma once

#include "CompatVtable.h"
#include "D3dDdiAdapterFuncsVisitor.h"

class CompatD3dDdiAdapterFuncs : public CompatVtable<CompatD3dDdiAdapterFuncs, D3dDdiAdapterFuncsIntf>
{
public:
	static void setCompatVtable(D3DDDI_ADAPTERFUNCS& vtable);
};
