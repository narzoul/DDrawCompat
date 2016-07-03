#pragma once

#include "CompatVtable.h"
#include "D3dDdiDeviceCallbacksVisitor.h"

class CompatD3dDdiDeviceCallbacks : public CompatVtable<CompatD3dDdiDeviceCallbacks, D3dDdiDeviceCallbacksIntf>
{
public:
	static void setCompatVtable(D3DDDI_DEVICECALLBACKS& vtable);
};
