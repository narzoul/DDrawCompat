#include "D3dDdi/Device.h"
#include "D3dDdi/DeviceFuncs.h"

namespace
{
	template <typename MethodPtr, MethodPtr deviceMethod, typename... Params>
	HRESULT WINAPI deviceFunc(HANDLE device, Params... params)
	{
		return (D3dDdi::Device::get(device).*deviceMethod)(params...);
	}

	template <typename MethodPtr, MethodPtr deviceStateMethod, typename... Params>
	HRESULT WINAPI deviceStateFunc(HANDLE device, Params... params)
	{
		return (D3dDdi::Device::get(device).getState().*deviceStateMethod)(params...);
	}

	HRESULT APIENTRY destroyDevice(HANDLE hDevice)
	{
		D3dDdi::Device::remove(hDevice);
		return D3dDdi::DeviceFuncs::s_origVtablePtr->pfnDestroyDevice(hDevice);
	}

	template <typename DeviceMethodPtr, DeviceMethodPtr deviceMethod, typename... Params>
	HRESULT APIENTRY flushPrimitives(HANDLE hDevice, Params... params)
	{
		D3dDdi::Device::get(hDevice).flushPrimitives();
		return (D3dDdi::DeviceFuncs::s_origVtablePtr->*deviceMethod)(hDevice, params...);
	}
}

#define DEVICE_FUNC(func) deviceFunc<decltype(&Device::func), &Device::func>
#define SET_DEVICE_STATE_FUNC(func) vtable.func = &deviceStateFunc<decltype(&DeviceState::func), &DeviceState::func>

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

		SET_DEVICE_STATE_FUNC(pfnDeletePixelShader);
		SET_DEVICE_STATE_FUNC(pfnDeleteVertexShaderDecl);
		SET_DEVICE_STATE_FUNC(pfnDeleteVertexShaderFunc);
		SET_DEVICE_STATE_FUNC(pfnSetPixelShader);
		SET_DEVICE_STATE_FUNC(pfnSetPixelShaderConst);
		SET_DEVICE_STATE_FUNC(pfnSetPixelShaderConstB);
		SET_DEVICE_STATE_FUNC(pfnSetPixelShaderConstI);
		SET_DEVICE_STATE_FUNC(pfnSetRenderState);
		SET_DEVICE_STATE_FUNC(pfnSetTexture);
		SET_DEVICE_STATE_FUNC(pfnSetTextureStageState);
		SET_DEVICE_STATE_FUNC(pfnSetVertexShaderConst);
		SET_DEVICE_STATE_FUNC(pfnSetVertexShaderConstB);
		SET_DEVICE_STATE_FUNC(pfnSetVertexShaderConstI);
		SET_DEVICE_STATE_FUNC(pfnSetVertexShaderDecl);
		SET_DEVICE_STATE_FUNC(pfnSetVertexShaderFunc);
		SET_DEVICE_STATE_FUNC(pfnSetZRange);
		SET_DEVICE_STATE_FUNC(pfnUpdateWInfo);

#define FLUSH_PRIMITIVES(func) vtable.func = &flushPrimitives<decltype(&D3DDDI_DEVICEFUNCS::func), &D3DDDI_DEVICEFUNCS::func>
		FLUSH_PRIMITIVES(pfnBufBlt);
		FLUSH_PRIMITIVES(pfnBufBlt1);
		FLUSH_PRIMITIVES(pfnDepthFill);
		FLUSH_PRIMITIVES(pfnDiscard);
		FLUSH_PRIMITIVES(pfnGenerateMipSubLevels);
		FLUSH_PRIMITIVES(pfnSetClipPlane);
		FLUSH_PRIMITIVES(pfnSetDepthStencil);
		FLUSH_PRIMITIVES(pfnSetPalette);
		FLUSH_PRIMITIVES(pfnSetScissorRect);
		FLUSH_PRIMITIVES(pfnSetViewport);
		FLUSH_PRIMITIVES(pfnStateSet);
		FLUSH_PRIMITIVES(pfnTexBlt);
		FLUSH_PRIMITIVES(pfnTexBlt1);
		FLUSH_PRIMITIVES(pfnUpdatePalette);
#undef  FLUSH_PRIMITIVES
	}
}
