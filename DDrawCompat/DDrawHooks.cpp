#define CINTERFACE

#include <d3d.h>

#include "CompatActivateAppHandler.h"
#include "CompatDirect3d.h"
#include "CompatDirect3dDevice.h"
#include "CompatDirectDraw.h"
#include "CompatDirectDrawSurface.h"
#include "CompatDirectDrawPalette.h"
#include "CompatPtr.h"
#include "CompatRef.h"
#include "DDrawHooks.h"
#include "DDrawLog.h"
#include "DDrawProcs.h"
#include "DDrawRepository.h"
#include "RealPrimarySurface.h"

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
			hookVtable<CompatDirect3d<IDirect3D>>(d3d);
			hookVtable<CompatDirect3d<IDirect3D2>>(d3d);
			hookVtable<CompatDirect3d<IDirect3D3>>(d3d);
			hookDirect3dDevice(*d3d, renderTarget);
		}
	}

	void hookDirect3d7(CompatRef<IDirectDraw7> dd, CompatRef<IDirectDrawSurface7> renderTarget)
	{
		CompatPtr<IDirect3D7> d3d(createDirect3d<IDirect3D7>(dd));
		if (d3d)
		{
			hookVtable<CompatDirect3d<IDirect3D7>>(d3d);
			hookDirect3dDevice7(*d3d, renderTarget);
		}
	}

	void hookDirect3dDevice(CompatRef<IDirect3D3> d3d, CompatRef<IDirectDrawSurface4> renderTarget)
	{
		CompatPtr<IDirect3DDevice3> d3dDevice(
			createDirect3dDevice<IDirect3DDevice3>(d3d, renderTarget, nullptr));

		hookVtable<CompatDirect3dDevice<IDirect3DDevice>>(d3dDevice);
		hookVtable<CompatDirect3dDevice<IDirect3DDevice2>>(d3dDevice);
		hookVtable<CompatDirect3dDevice<IDirect3DDevice3>>(d3dDevice);
	}

	void hookDirect3dDevice7(CompatRef<IDirect3D7> d3d, CompatRef<IDirectDrawSurface7> renderTarget)
	{
		CompatPtr<IDirect3DDevice7> d3dDevice(
			createDirect3dDevice<IDirect3DDevice7>(d3d, renderTarget));

		hookVtable<CompatDirect3dDevice<IDirect3DDevice7>>(d3dDevice);
	}

	void hookDirectDraw(CompatRef<IDirectDraw7> dd)
	{
		CompatDirectDraw<IDirectDraw7>::s_origVtable = *(&dd)->lpVtbl;
		CompatPtr<IDirectDraw7> dd7(&dd);
		hookVtable<CompatDirectDraw<IDirectDraw>>(dd7);
		hookVtable<CompatDirectDraw<IDirectDraw2>>(dd7);
		hookVtable<CompatDirectDraw<IDirectDraw4>>(dd7);
		hookVtable<CompatDirectDraw<IDirectDraw7>>(dd7);
		dd7.detach();
	}

	void hookDirectDrawPalette(CompatRef<IDirectDraw7> dd)
	{
		PALETTEENTRY paletteEntries[2] = {};
		CompatPtr<IDirectDrawPalette> palette;
		HRESULT result = dd->CreatePalette(&dd,
			DDPCAPS_1BIT, paletteEntries, &palette.getRef(), nullptr);
		if (SUCCEEDED(result))
		{
			CompatDirectDrawPalette::hookVtable(*palette);
		}
		else
		{
			Compat::Log() << "Failed to create a DirectDraw palette for hooking: " << result;
		}
	}

	void hookDirectDrawSurface(CompatRef<IDirectDraw7> dd)
	{
		DDSURFACEDESC2 desc = {};
		desc.dwSize = sizeof(desc);
		desc.dwFlags = DDSD_WIDTH | DDSD_HEIGHT | DDSD_CAPS;
		desc.dwWidth = 1;
		desc.dwHeight = 1;
		desc.ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN;

		CompatPtr<IDirectDrawSurface7> surface;
		HRESULT result = dd->CreateSurface(&dd, &desc, &surface.getRef(), nullptr);
		if (SUCCEEDED(result))
		{
			CompatDirectDrawSurface<IDirectDrawSurface7>::s_origVtable = *surface.get()->lpVtbl;
			hookVtable<CompatDirectDrawSurface<IDirectDrawSurface>>(surface);
			hookVtable<CompatDirectDrawSurface<IDirectDrawSurface2>>(surface);
			hookVtable<CompatDirectDrawSurface<IDirectDrawSurface3>>(surface);
			hookVtable<CompatDirectDrawSurface<IDirectDrawSurface4>>(surface);
			hookVtable<CompatDirectDrawSurface<IDirectDrawSurface7>>(surface);
		}
		else
		{
			Compat::Log() << "Failed to create a DirectDraw surface for hooking: " << result;
		}
	}

	template <typename CompatInterface>
	void hookVtable(const CompatPtr<typename CompatInterface::Interface>& intf)
	{
		CompatInterface::hookVtable(*intf);
	}
}

namespace DDrawHooks
{
	void installHooks()
	{
		CompatPtr<IDirectDraw> dd;
		CALL_ORIG_DDRAW(DirectDrawCreate, nullptr, &dd.getRef(), nullptr);
		if (!dd)
		{
			Compat::Log() << "Failed to create a DirectDraw object for hooking";
			return;
		}

		HRESULT result = dd->SetCooperativeLevel(dd, nullptr, DDSCL_NORMAL);
		if (FAILED(result))
		{
			Compat::Log() << "Failed to set the cooperative level for hooking: " << result;
			return;
		}

		auto dd7(DDrawRepository::getDirectDraw());
		if (!dd7)
		{
			Compat::Log() << "Failed to create a DirectDraw7 object for hooking";
			return;
		}

		hookDirectDraw(*dd7);
		hookDirectDrawSurface(*dd7);
		hookDirectDrawPalette(*dd7);

		CompatPtr<IDirectDrawSurface7> renderTarget7(createRenderTarget(*dd7));
		if (renderTarget7)
		{
			CompatPtr<IDirectDrawSurface4> renderTarget4(renderTarget7);
			hookDirect3d(*dd, *renderTarget4);
			hookDirect3d7(*dd7, *renderTarget7);
		}

		CompatActivateAppHandler::installHooks();
	}

	void uninstallHooks()
	{
		RealPrimarySurface::removeUpdateThread();
		CompatActivateAppHandler::uninstallHooks();
	}
}
