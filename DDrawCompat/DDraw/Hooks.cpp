#include "Common/CompatRef.h"
#include "Common/Log.h"
#include "DDraw/DirectDraw.h"
#include "DDraw/DirectDrawClipper.h"
#include "DDraw/DirectDrawGammaControl.h"
#include "DDraw/DirectDrawPalette.h"
#include "DDraw/DirectDrawSurface.h"
#include "DDraw/Hooks.h"
#include "DDraw/RealPrimarySurface.h"
#include "Win32/Registry.h"

namespace
{
	template <typename Interface>
	void hookVtable(const CompatPtr<Interface>& intf);

	void hookDirectDraw(CompatPtr<IDirectDraw7> dd)
	{
		hookVtable<IDirectDraw>(dd);
		hookVtable<IDirectDraw2>(dd);
		hookVtable<IDirectDraw4>(dd);
		hookVtable<IDirectDraw7>(dd);
	}

	void hookDirectDrawClipper(CompatRef<IDirectDraw7> dd)
	{
		CompatPtr<IDirectDrawClipper> clipper;
		HRESULT result = dd->CreateClipper(&dd, 0, &clipper.getRef(), nullptr);
		if (SUCCEEDED(result))
		{
			DDraw::DirectDrawClipper::hookVtable(clipper.get()->lpVtbl);
		}
		else
		{
			Compat::Log() << "ERROR: Failed to create a DirectDraw clipper for hooking: " << result;
		}
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
			Compat::Log() << "ERROR: Failed to create a DirectDraw palette for hooking: " << result;
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
			hookVtable<IDirectDrawSurface>(surface);
			hookVtable<IDirectDrawSurface2>(surface);
			hookVtable<IDirectDrawSurface3>(surface);
			hookVtable<IDirectDrawSurface4>(surface);
			hookVtable<IDirectDrawSurface7>(surface);
			hookVtable<IDirectDrawGammaControl>(surface);
		}
		else
		{
			Compat::Log() << "ERROR: Failed to create a DirectDraw surface for hooking: " << result;
		}
	}

	template <typename Interface>
	void hookVtable(const CompatPtr<Interface>& intf)
	{
		if (intf)
		{
			CompatVtable<Vtable<Interface>>::hookVtable(intf.get()->lpVtbl);
		}
	}
}

namespace DDraw
{
	void installHooks(CompatPtr<IDirectDraw7> dd7)
	{
		RealPrimarySurface::init();

		Win32::Registry::unsetValue(
			HKEY_LOCAL_MACHINE, "SOFTWARE\\Microsoft\\DirectDraw", "EmulationOnly");
		Win32::Registry::unsetValue(
			HKEY_LOCAL_MACHINE, "SOFTWARE\\WOW6432Node\\Microsoft\\DirectDraw", "EmulationOnly");

		hookDirectDraw(dd7);
		hookDirectDrawClipper(*dd7);
		hookDirectDrawPalette(*dd7);
		hookDirectDrawSurface(*dd7);
	}

	void uninstallHooks()
	{
		RealPrimarySurface::removeUpdateThread();
	}
}
