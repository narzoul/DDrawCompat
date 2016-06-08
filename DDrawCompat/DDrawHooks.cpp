#define CINTERFACE

#include <d3d.h>

#include "CompatActivateAppHandler.h"
#include "CompatDirect3d.h"
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

	void hookDirect3d(CompatRef<IDirectDraw> dd)
	{
		CompatPtr<IDirect3D> d3d(createDirect3d<IDirect3D>(dd));
		if (d3d)
		{
			hookVtable<CompatDirect3d<IDirect3D>>(d3d);
			hookVtable<CompatDirect3d<IDirect3D2>>(d3d);
			hookVtable<CompatDirect3d<IDirect3D3>>(d3d);
		}
	}

	void hookDirect3d7(CompatRef<IDirectDraw7> dd)
	{
		CompatPtr<IDirect3D7> d3d(createDirect3d<IDirect3D7>(dd));
		if (d3d)
		{
			hookVtable<CompatDirect3d<IDirect3D7>>(d3d);
		}
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

		auto dd7(DDrawRepository::getDirectDraw());
		if (!dd7)
		{
			Compat::Log() << "Failed to create a DirectDraw7 object for hooking";
			return;
		}

		hookDirectDraw(*dd7);
		hookDirectDrawSurface(*dd7);
		hookDirectDrawPalette(*dd7);
		hookDirect3d(*dd);
		hookDirect3d7(*dd7);
		CompatActivateAppHandler::installHooks();
	}

	void uninstallHooks()
	{
		RealPrimarySurface::removeUpdateThread();
		CompatActivateAppHandler::uninstallHooks();
	}
}
