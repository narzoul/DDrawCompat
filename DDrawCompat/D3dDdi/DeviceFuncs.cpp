#include <map>

#include "D3dDdi/Device.h"
#include "D3dDdi/DeviceFuncs.h"

namespace
{
	std::map<HANDLE, D3dDdi::Device> g_devices;

	D3dDdi::Device& getDevice(HANDLE device)
	{
		auto it = g_devices.find(device);
		if (it != g_devices.end())
		{
			return it->second;
		}

		return g_devices.emplace(device, D3dDdi::Device(nullptr, device)).first->second;
	}

	template <typename DeviceMethodPtr, DeviceMethodPtr deviceMethod, typename Arg, typename... Params>
	HRESULT WINAPI deviceFunc(HANDLE device, Arg* data, Params... params)
	{
		return (getDevice(device).*deviceMethod)(*data, params...);
	}

	HRESULT APIENTRY destroyDevice(HANDLE hDevice)
	{
		HRESULT result = D3dDdi::DeviceFuncs::s_origVtables.at(hDevice).pfnDestroyDevice(hDevice);
		if (SUCCEEDED(result))
		{
			D3dDdi::DeviceFuncs::s_origVtables.erase(hDevice);
			g_devices.erase(hDevice);
		}
		return result;
	}

	HRESULT APIENTRY destroyResource(HANDLE hDevice, HANDLE hResource)
	{
		return getDevice(hDevice).destroyResource(hResource);
	}
}

#define DEVICE_FUNC(func) deviceFunc<decltype(&Device::func), &Device::func>

namespace D3dDdi
{
	void DeviceFuncs::onCreateDevice(HANDLE adapter, HANDLE device)
	{
		g_devices.emplace(device, Device(adapter, device));
	}

	void DeviceFuncs::setCompatVtable(D3DDDI_DEVICEFUNCS& vtable)
	{
		vtable.pfnBlt = &DEVICE_FUNC(blt);
		vtable.pfnClear = &DEVICE_FUNC(clear);
		vtable.pfnColorFill = &DEVICE_FUNC(colorFill);
		vtable.pfnCreateResource = &DEVICE_FUNC(createResource);
		vtable.pfnCreateResource2 = &DEVICE_FUNC(createResource2);
		vtable.pfnDestroyDevice = &destroyDevice;
		vtable.pfnDestroyResource = &destroyResource;
		vtable.pfnDrawIndexedPrimitive = &DEVICE_FUNC(drawIndexedPrimitive);
		vtable.pfnDrawIndexedPrimitive2 = &DEVICE_FUNC(drawIndexedPrimitive2);
		vtable.pfnDrawPrimitive = &DEVICE_FUNC(drawPrimitive);
		vtable.pfnDrawPrimitive2 = &DEVICE_FUNC(drawPrimitive2);
		vtable.pfnDrawRectPatch = &DEVICE_FUNC(drawRectPatch);
		vtable.pfnDrawTriPatch = &DEVICE_FUNC(drawTriPatch);
		vtable.pfnLock = &DEVICE_FUNC(lock);
		vtable.pfnOpenResource = &DEVICE_FUNC(openResource);
		vtable.pfnPresent = &DEVICE_FUNC(present);
		vtable.pfnPresent1 = &DEVICE_FUNC(present1);
		vtable.pfnTexBlt = &DEVICE_FUNC(texBlt);
		vtable.pfnTexBlt1 = &DEVICE_FUNC(texBlt1);
		vtable.pfnUnlock = &DEVICE_FUNC(unlock);
		vtable.pfnUpdateWInfo = &DEVICE_FUNC(updateWInfo);
	}
}
