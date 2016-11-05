#include "D3dDdi/DeviceFuncs.h"

namespace
{
	D3DDDI_DEVICEFUNCS& getOrigVtable(HANDLE device);
	D3DDDI_RESOURCEFLAGS getResourceTypeFlags();
	void modifyCreateResourceFlags(D3DDDI_RESOURCEFLAGS& flags);

	const UINT g_resourceTypeFlags = getResourceTypeFlags().Value;

	HRESULT APIENTRY createResource(HANDLE hDevice, D3DDDIARG_CREATERESOURCE* pResource)
	{
		modifyCreateResourceFlags(pResource->Flags);
		return getOrigVtable(hDevice).pfnCreateResource(hDevice, pResource);
	}

	HRESULT APIENTRY createResource2(HANDLE hDevice, D3DDDIARG_CREATERESOURCE2* pResource2)
	{
		modifyCreateResourceFlags(pResource2->Flags);
		return getOrigVtable(hDevice).pfnCreateResource2(hDevice, pResource2);
	}

	HRESULT APIENTRY destroyDevice(HANDLE hDevice)
	{
		HRESULT result = getOrigVtable(hDevice).pfnDestroyDevice(hDevice);
		if (SUCCEEDED(result))
		{
			D3dDdi::DeviceFuncs::s_origVtables.erase(hDevice);
		}
		return result;
	}

	D3DDDI_DEVICEFUNCS& getOrigVtable(HANDLE device)
	{
		return D3dDdi::DeviceFuncs::s_origVtables.at(device);
	}

	D3DDDI_RESOURCEFLAGS getResourceTypeFlags()
	{
		D3DDDI_RESOURCEFLAGS flags = {};
		flags.RenderTarget = 1;
		flags.ZBuffer = 1;
		flags.DMap = 1;
		flags.Points = 1;
		flags.RtPatches = 1;
		flags.NPatches = 1;
		flags.Video = 1;
		flags.CaptureBuffer = 1;
		flags.Primary = 1;
		flags.Texture = 1;
		flags.CubeMap = 1;
		flags.VertexBuffer = 1;
		flags.IndexBuffer = 1;
		flags.DecodeRenderTarget = 1;
		flags.DecodeCompressedBuffer = 1;
		flags.VideoProcessRenderTarget = 1;
		flags.Overlay = 1;
		flags.TextApi = 1;
		return flags;
	}

	void modifyCreateResourceFlags(D3DDDI_RESOURCEFLAGS& flags)
	{
		const bool isOffScreenPlain = 0 == (flags.Value & g_resourceTypeFlags);
		if (isOffScreenPlain)
		{
			flags.CpuOptimized = 1;
		}
	}
}

namespace D3dDdi
{
	void DeviceFuncs::setCompatVtable(D3DDDI_DEVICEFUNCS& vtable)
	{
		vtable.pfnCreateResource = &createResource;
		vtable.pfnCreateResource2 = &createResource2;
		vtable.pfnDestroyDevice = &destroyDevice;
	}
}
