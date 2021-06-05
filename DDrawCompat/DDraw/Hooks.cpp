#include <Common/CompatRef.h>
#include <Common/Log.h>
#include <DDraw/DirectDraw.h>
#include <DDraw/DirectDrawClipper.h>
#include <DDraw/DirectDrawGammaControl.h>
#include <DDraw/DirectDrawPalette.h>
#include <DDraw/DirectDrawSurface.h>
#include <DDraw/Hooks.h>
#include <DDraw/RealPrimarySurface.h>
#include <DDraw/ScopedThreadLock.h>
#include <Win32/Registry.h>

namespace
{
	decltype(IDirectDraw7Vtbl::Initialize) g_origInitialize = nullptr;

	void hookDirectDraw(CompatPtr<IDirectDraw7> dd)
	{
		DDraw::DirectDraw::hookVtable(*CompatPtr<IDirectDraw>(dd).get()->lpVtbl);
		DDraw::DirectDraw::hookVtable(*CompatPtr<IDirectDraw2>(dd).get()->lpVtbl);
		DDraw::DirectDraw::hookVtable(*CompatPtr<IDirectDraw4>(dd).get()->lpVtbl);
		DDraw::DirectDraw::hookVtable(*CompatPtr<IDirectDraw7>(dd).get()->lpVtbl);
	}

	void hookDirectDrawClipper(CompatRef<IDirectDraw7> dd)
	{
		CompatPtr<IDirectDrawClipper> clipper;
		HRESULT result = dd->CreateClipper(&dd, 0, &clipper.getRef(), nullptr);
		if (SUCCEEDED(result))
		{
			DDraw::DirectDrawClipper::hookVtable(*clipper.get()->lpVtbl);
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
		HRESULT result = dd->CreatePalette(&dd, DDPCAPS_1BIT, paletteEntries, &palette.getRef(), nullptr);
		if (SUCCEEDED(result))
		{
			DDraw::DirectDrawPalette::hookVtable(*palette.get()->lpVtbl);
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
			CompatVtable<IDirectDrawSurface7Vtbl>::s_origVtable = *surface.get()->lpVtbl;
			DDraw::DirectDrawSurface::hookVtable(*CompatPtr<IDirectDrawSurface>(surface).get()->lpVtbl);
			DDraw::DirectDrawSurface::hookVtable(*CompatPtr<IDirectDrawSurface2>(surface).get()->lpVtbl);
			DDraw::DirectDrawSurface::hookVtable(*CompatPtr<IDirectDrawSurface3>(surface).get()->lpVtbl);
			DDraw::DirectDrawSurface::hookVtable(*CompatPtr<IDirectDrawSurface4>(surface).get()->lpVtbl);
			DDraw::DirectDrawSurface::hookVtable(*CompatPtr<IDirectDrawSurface7>(surface).get()->lpVtbl);

			CompatPtr<IDirectDrawGammaControl> gammaControl(surface);
			DDraw::DirectDrawGammaControl::hookVtable(*gammaControl.get()->lpVtbl);
		}
		else
		{
			Compat::Log() << "ERROR: Failed to create a DirectDraw surface for hooking: " << result;
		}
	}

	HRESULT STDMETHODCALLTYPE initialize(IUnknown* This, GUID* lpGUID)
	{
		LOG_FUNC("IDirectDrawVtbl::Initialize", This, lpGUID);
		LOG_ONCE("COM instantiation of DirectDraw detected");
		DDraw::DirectDraw::suppressEmulatedDirectDraw(lpGUID);
		HRESULT result = g_origInitialize(reinterpret_cast<IDirectDraw7*>(This), lpGUID);
		if (SUCCEEDED(result))
		{
			DDraw::DirectDraw::onCreate(lpGUID, *CompatPtr<IDirectDraw7>::from(This));
		}
		return result;
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

		g_origInitialize = dd7.get()->lpVtbl->Initialize;
		Compat::hookFunction(reinterpret_cast<void*&>(g_origInitialize), initialize, "IDirectDrawVtbl::Initialize");

		hookDirectDraw(dd7);
		hookDirectDrawClipper(*dd7);
		hookDirectDrawPalette(*dd7);
		hookDirectDrawSurface(*dd7);
	}
}
