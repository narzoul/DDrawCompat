#define CINTERFACE

#include <d3d.h>

#include "Common/CompatPtr.h"
#include "Common/CompatRef.h"
#include "Common/Log.h"
#include "DDraw/Repository.h"
#include "Direct3d/Direct3d.h"
#include "Direct3d/Direct3dDevice.h"
#include "Direct3d/Hooks.h"
#include "Dll/Procs.h"

namespace
{
	void hookDirect3dDevice(CompatRef<IDirect3D3> d3d, CompatRef<IDirectDrawSurface4> renderTarget);
	void hookDirect3dDevice7(CompatRef<IDirect3D7> d3d, CompatRef<IDirectDrawSurface7> renderTarget);

	template <typename CompatInterface>
	void hookVtable(const CompatPtr<typename CompatInterface::Interface>& intf);

	template <typename TDirect3d, typename TDirectDraw>
	CompatPtr<TDirect3d> createDirect3d(CompatRef<TDirectDraw> dd)
	{
		CompatPtr<TDirect3d> d3d;
		HRESULT result = dd->QueryInterface(&dd, Compat::getIntfId<TDirect3d>(),
			reinterpret_cast<void**>(&d3d.getRef()));
		if (FAILED(result))
		{
			Compat::Log() << "Failed to create a Direct3D object for hooking: " << result;
		}
		return d3d;
	}

	template <typename TDirect3dDevice, typename TDirect3d, typename TDirectDrawSurface,
		typename... Params>
		CompatPtr<TDirect3dDevice> createDirect3dDevice(
			CompatRef<TDirect3d> d3d, CompatRef<TDirectDrawSurface> renderTarget, Params... params)
	{
		CompatPtr<TDirect3dDevice> d3dDevice;
		HRESULT result = d3d->CreateDevice(
			&d3d, IID_IDirect3DRGBDevice, &renderTarget, &d3dDevice.getRef(), params...);
		if (FAILED(result))
		{
			Compat::Log() << "Failed to create a Direct3D device for hooking: " << result;
		}
		return d3dDevice;
	}

	CompatPtr<IDirectDrawSurface7> createRenderTarget(CompatRef<IDirectDraw7> dd)
	{
		DDSURFACEDESC2 desc = {};
		desc.dwSize = sizeof(desc);
		desc.dwFlags = DDSD_WIDTH | DDSD_HEIGHT | DDSD_PIXELFORMAT | DDSD_CAPS;
		desc.dwWidth = 1;
		desc.dwHeight = 1;
		desc.ddpfPixelFormat.dwSize = sizeof(desc.ddpfPixelFormat);
		desc.ddpfPixelFormat.dwFlags = DDPF_RGB;
		desc.ddpfPixelFormat.dwRGBBitCount = 32;
		desc.ddpfPixelFormat.dwRBitMask = 0x00FF0000;
		desc.ddpfPixelFormat.dwGBitMask = 0x0000FF00;
		desc.ddpfPixelFormat.dwBBitMask = 0x000000FF;
		desc.ddsCaps.dwCaps = DDSCAPS_3DDEVICE;

		CompatPtr<IDirectDrawSurface7> renderTarget;
		HRESULT result = dd->CreateSurface(&dd, &desc, &renderTarget.getRef(), nullptr);
		if (FAILED(result))
		{
			Compat::Log() << "Failed to create a render target for hooking: " << result;
		}
		return renderTarget;
	}

	void hookDirect3d(CompatRef<IDirectDraw> dd, CompatRef<IDirectDrawSurface4> renderTarget)
	{
		CompatPtr<IDirect3D3> d3d(createDirect3d<IDirect3D3>(dd));
		if (d3d)
		{
			hookVtable<Direct3d::Direct3d<IDirect3D>>(d3d);
			hookVtable<Direct3d::Direct3d<IDirect3D2>>(d3d);
			hookVtable<Direct3d::Direct3d<IDirect3D3>>(d3d);
			hookDirect3dDevice(*d3d, renderTarget);
		}
	}

	void hookDirect3d7(CompatRef<IDirectDraw7> dd, CompatRef<IDirectDrawSurface7> renderTarget)
	{
		CompatPtr<IDirect3D7> d3d(createDirect3d<IDirect3D7>(dd));
		if (d3d)
		{
			hookVtable<Direct3d::Direct3d<IDirect3D7>>(d3d);
			hookDirect3dDevice7(*d3d, renderTarget);
		}
	}

	void hookDirect3dDevice(CompatRef<IDirect3D3> d3d, CompatRef<IDirectDrawSurface4> renderTarget)
	{
		CompatPtr<IDirect3DDevice3> d3dDevice(
			createDirect3dDevice<IDirect3DDevice3>(d3d, renderTarget, nullptr));

		hookVtable<Direct3d::Direct3dDevice<IDirect3DDevice>>(d3dDevice);
		hookVtable<Direct3d::Direct3dDevice<IDirect3DDevice2>>(d3dDevice);
		hookVtable<Direct3d::Direct3dDevice<IDirect3DDevice3>>(d3dDevice);
	}

	void hookDirect3dDevice7(CompatRef<IDirect3D7> d3d, CompatRef<IDirectDrawSurface7> renderTarget)
	{
		CompatPtr<IDirect3DDevice7> d3dDevice(
			createDirect3dDevice<IDirect3DDevice7>(d3d, renderTarget));

		hookVtable<Direct3d::Direct3dDevice<IDirect3DDevice7>>(d3dDevice);
	}

	template <typename CompatInterface>
	void hookVtable(const CompatPtr<typename CompatInterface::Interface>& intf)
	{
		CompatInterface::hookVtable(intf.get()->lpVtbl);
	}
}

namespace Direct3d
{
	void installHooks()
	{
		auto dd7(DDraw::Repository::getDirectDraw());
		CompatPtr<IDirectDraw> dd;
		CALL_ORIG_PROC(DirectDrawCreate, nullptr, &dd.getRef(), nullptr);
		if (!dd || !dd7 || FAILED(dd->SetCooperativeLevel(dd, nullptr, DDSCL_NORMAL)))
		{
			Compat::Log() << "Failed to hook Direct3d interfaces";
			return;
		}

		CompatPtr<IDirectDrawSurface7> renderTarget7(createRenderTarget(*dd7));
		if (renderTarget7)
		{
			CompatPtr<IDirectDrawSurface4> renderTarget4(renderTarget7);
			hookDirect3d(*dd, *renderTarget4);
			hookDirect3d7(*dd7, *renderTarget7);
		}
	}
}
