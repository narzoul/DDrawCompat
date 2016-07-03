#include "CompatD3dDdiAdapterFuncs.h"
#include "CompatD3dDdiDeviceCallbacks.h"
#include "CompatD3dDdiDeviceFuncs.h"

namespace
{
	HRESULT APIENTRY createDevice(HANDLE hAdapter, D3DDDIARG_CREATEDEVICE* pCreateData)
	{
		CompatD3dDdiDeviceCallbacks::hookVtable(pCreateData->pCallbacks);
		HRESULT result = CompatD3dDdiAdapterFuncs::s_origVtable.pfnCreateDevice(hAdapter, pCreateData);
		if (SUCCEEDED(result))
		{
			CompatD3dDdiDeviceFuncs::hookVtable(pCreateData->pDeviceFuncs);
		}
		return result;
	}
}

void CompatD3dDdiAdapterFuncs::setCompatVtable(D3DDDI_ADAPTERFUNCS& vtable)
{
	vtable.pfnCreateDevice = &createDevice;
}
