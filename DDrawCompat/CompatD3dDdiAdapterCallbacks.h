#pragma once

#include "CompatVtable.h"
#include "D3dDdiAdapterCallbacksVisitor.h"

class CompatD3dDdiAdapterCallbacks :
	public CompatVtable<CompatD3dDdiAdapterCallbacks, D3dDdiAdapterCallbacksIntf>
{
public:
	static void setCompatVtable(D3DDDI_ADAPTERCALLBACKS& vtable);
};
