#include "D3dDdi/Device.h"
#include "D3dDdi/DeviceFuncs.h"

namespace
{
	template <typename DeviceMethodPtr, DeviceMethodPtr deviceMethod, typename... Params>
	HRESULT WINAPI deviceFunc(HANDLE device, Params... params)
	{
		return (D3dDdi::Device::get(device).*deviceMethod)(params...);
	}

	HRESULT APIENTRY destroyDevice(HANDLE hDevice)
	{
		D3dDdi::Device::remove(hDevice);
		return D3dDdi::DeviceFuncs::s_origVtablePtr->pfnDestroyDevice(hDevice);
	}
}

#define DEVICE_FUNC(func) deviceFunc<decltype(&Device::func), &Device::func>

namespace D3dDdi
{
	void DeviceFuncs::onCreateDevice(HANDLE adapter, HANDLE device)
	{
		Device::add(adapter, device);
	}

	void DeviceFuncs::setCompatVtable(D3DDDI_DEVICEFUNCS& vtable)
	{
		vtable.pfnBlt = &DEVICE_FUNC(blt);
		vtable.pfnClear = &DEVICE_FUNC(clear);
		vtable.pfnColorFill = &DEVICE_FUNC(colorFill);
		vtable.pfnCreateResource = &DEVICE_FUNC(createResource);
		vtable.pfnCreateResource2 = &DEVICE_FUNC(createResource2);
		vtable.pfnDestroyDevice = &destroyDevice;
		vtable.pfnDestroyResource = &DEVICE_FUNC(destroyResource);
		vtable.pfnDrawIndexedPrimitive2 = &DEVICE_FUNC(drawIndexedPrimitive2);
		vtable.pfnDrawPrimitive = &DEVICE_FUNC(drawPrimitive);
		vtable.pfnFlush = &DEVICE_FUNC(flush);
		vtable.pfnFlush1 = &DEVICE_FUNC(flush1);
		vtable.pfnLock = &DEVICE_FUNC(lock);
		vtable.pfnOpenResource = &DEVICE_FUNC(openResource);
		vtable.pfnPresent = &DEVICE_FUNC(present);
		vtable.pfnPresent1 = &DEVICE_FUNC(present1);
		vtable.pfnSetRenderTarget = &DEVICE_FUNC(setRenderTarget);
		vtable.pfnSetStreamSource = &DEVICE_FUNC(setStreamSource);
		vtable.pfnSetStreamSourceUm = &DEVICE_FUNC(setStreamSourceUm);
		vtable.pfnUnlock = &DEVICE_FUNC(unlock);
		vtable.pfnUpdateWInfo = &DEVICE_FUNC(updateWInfo);
	}
}
