#include "D3dDdi/AdapterFuncs.h"
#include "D3dDdi/DeviceCallbacks.h"
#include "D3dDdi/DeviceFuncs.h"

namespace
{
	HRESULT APIENTRY createDevice(HANDLE hAdapter, D3DDDIARG_CREATEDEVICE* pCreateData)
	{
		D3dDdi::DeviceCallbacks::hookVtable(pCreateData->pCallbacks);
		HRESULT result = D3dDdi::AdapterFuncs::s_origVtable.pfnCreateDevice(
			hAdapter, pCreateData);
		if (SUCCEEDED(result))
		{
			D3dDdi::DeviceFuncs::hookVtable(pCreateData->pDeviceFuncs);
		}
		return result;
	}
}

namespace D3dDdi
{
	void AdapterFuncs::setCompatVtable(D3DDDI_ADAPTERFUNCS& vtable)
	{
		vtable.pfnCreateDevice = &createDevice;
	}
}
