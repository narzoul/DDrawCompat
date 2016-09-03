#define CINTERFACE

#include <d3d.h>

#include "Common/CompatPtr.h"
#include "Common/CompatRef.h"
#include "Common/Log.h"
#include "DDraw/ActivateAppHandler.h"
#include "DDraw/DirectDraw.h"
#include "DDraw/DirectDrawSurface.h"
#include "DDraw/DirectDrawPalette.h"
#include "DDraw/Hooks.h"
#include "DDraw/RealPrimarySurface.h"
#include "DDraw/Repository.h"
#include "Dll/Procs.h"

namespace
{
	template <typename CompatInterface>
	void hookVtable(const CompatPtr<typename CompatInterface::Interface>& intf);

	void hookDirectDraw(CompatRef<IDirectDraw7> dd)
	{
		DDraw::DirectDraw<IDirectDraw7>::s_origVtable = *(&dd)->lpVtbl;
		CompatPtr<IDirectDraw7> dd7(&dd);
		hookVtable<DDraw::DirectDraw<IDirectDraw>>(dd7);
		hookVtable<DDraw::DirectDraw<IDirectDraw2>>(dd7);
		hookVtable<DDraw::DirectDraw<IDirectDraw4>>(dd7);
		hookVtable<DDraw::DirectDraw<IDirectDraw7>>(dd7);
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
			DDraw::DirectDrawPalette::hookVtable(palette.get()->lpVtbl);
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
			DDraw::DirectDrawSurface<IDirectDrawSurface7>::s_origVtable = *surface.get()->lpVtbl;
			hookVtable<DDraw::DirectDrawSurface<IDirectDrawSurface>>(surface);
			hookVtable<DDraw::DirectDrawSurface<IDirectDrawSurface2>>(surface);
			hookVtable<DDraw::DirectDrawSurface<IDirectDrawSurface3>>(surface);
			hookVtable<DDraw::DirectDrawSurface<IDirectDrawSurface4>>(surface);
			hookVtable<DDraw::DirectDrawSurface<IDirectDrawSurface7>>(surface);
		}
		else
		{
			Compat::Log() << "Failed to create a DirectDraw surface for hooking: " << result;
		}
	}

	template <typename CompatInterface>
	void hookVtable(const CompatPtr<typename CompatInterface::Interface>& intf)
	{
		CompatInterface::hookVtable(intf.get()->lpVtbl);
	}
}

namespace DDraw
{
	void installHooks()
	{
		CompatPtr<IDirectDraw> dd;
		CALL_ORIG_PROC(DirectDrawCreate, nullptr, &dd.getRef(), nullptr);
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

		auto dd7(Repository::getDirectDraw());
		if (!dd7)
		{
			Compat::Log() << "Failed to create a DirectDraw7 object for hooking";
			return;
		}

		hookDirectDraw(*dd7);
		hookDirectDrawSurface(*dd7);
		hookDirectDrawPalette(*dd7);

		ActivateAppHandler::installHooks();
	}

	void uninstallHooks()
	{
		RealPrimarySurface::removeUpdateThread();
		ActivateAppHandler::uninstallHooks();
	}
}
