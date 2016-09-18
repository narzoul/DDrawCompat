#include "D3dDdi/AdapterFuncs.h"
#include "D3dDdi/DeviceCallbacks.h"
#include "D3dDdi/DeviceFuncs.h"

namespace
{
	HRESULT APIENTRY closeAdapter(HANDLE hAdapter)
	{
		HRESULT result = D3dDdi::AdapterFuncs::s_origVtables.at(hAdapter).pfnCloseAdapter(hAdapter);
		if (SUCCEEDED(result))
		{
			D3dDdi::AdapterFuncs::s_origVtables.erase(hAdapter);
		}
		return result;
	}

	HRESULT APIENTRY createDevice(HANDLE hAdapter, D3DDDIARG_CREATEDEVICE* pCreateData)
	{
		D3dDdi::DeviceCallbacks::hookVtable(pCreateData->pCallbacks);
		HRESULT result = D3dDdi::AdapterFuncs::s_origVtables.at(hAdapter).pfnCreateDevice(
			hAdapter, pCreateData);
		if (SUCCEEDED(result))
		{
			D3dDdi::DeviceFuncs::hookDriverVtable(pCreateData->hDevice, pCreateData->pDeviceFuncs);
		}
		return result;
	}
}

namespace D3dDdi
{
	void AdapterFuncs::setCompatVtable(D3DDDI_ADAPTERFUNCS& vtable)
	{
		vtable.pfnCloseAdapter = &closeAdapter;
		vtable.pfnCreateDevice = &createDevice;
	}
}
