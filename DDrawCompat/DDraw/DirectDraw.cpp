#include <Common/CompatPtr.h>
#include <D3dDdi/KernelModeThunks.h>
#include <DDraw/DirectDraw.h>
#include <DDraw/Surfaces/PrimarySurface.h>
#include <Win32/DisplayMode.h>

namespace DDraw
{
	DDSURFACEDESC2 getDisplayMode(CompatRef<IDirectDraw7> dd)
	{
		DDSURFACEDESC2 dm = {};
		dm.dwSize = sizeof(dm);
		dd->GetDisplayMode(&dd, &dm);
		return dm;
	}

	DDPIXELFORMAT getRgbPixelFormat(DWORD bpp)
	{
		DDPIXELFORMAT pf = {};
		pf.dwSize = sizeof(pf);
		pf.dwFlags = DDPF_RGB;
		pf.dwRGBBitCount = bpp;

		switch (bpp)
		{
		case 1:
			pf.dwFlags |= DDPF_PALETTEINDEXED1;
			break;
		case 2:
			pf.dwFlags |= DDPF_PALETTEINDEXED2;
			break;
		case 4:
			pf.dwFlags |= DDPF_PALETTEINDEXED4;
			break;
		case 8:
			pf.dwFlags |= DDPF_PALETTEINDEXED8;
			break;
		case 16:
			pf.dwRBitMask = 0xF800;
			pf.dwGBitMask = 0x07E0;
			pf.dwBBitMask = 0x001F;
			break;
		case 24:
		case 32:
			pf.dwRBitMask = 0xFF0000;
			pf.dwGBitMask = 0x00FF00;
			pf.dwBBitMask = 0x0000FF;
			break;
		}

		return pf;
	}

	void logComInstantiation()
	{
		LOG_ONCE("COM instantiation of DirectDraw detected");
	}

	void suppressEmulatedDirectDraw(GUID*& guid)
	{
		if (reinterpret_cast<GUID*>(DDCREATE_EMULATIONONLY) == guid)
		{
			LOG_ONCE("Suppressed a request to create an emulated DirectDraw object");
			guid = nullptr;
		}
	}

	template <typename TDirectDraw>
	void DirectDraw<TDirectDraw>::setCompatVtable(Vtable<TDirectDraw>& vtable)
	{
		vtable.CreateSurface = &CreateSurface;
		vtable.FlipToGDISurface = &FlipToGDISurface;
		vtable.GetGDISurface = &GetGDISurface;
		vtable.WaitForVerticalBlank = &WaitForVerticalBlank;
	}

	template <typename TDirectDraw>
	HRESULT STDMETHODCALLTYPE DirectDraw<TDirectDraw>::CreateSurface(
		TDirectDraw* This,
		TSurfaceDesc* lpDDSurfaceDesc,
		TSurface** lplpDDSurface,
		IUnknown* pUnkOuter)
	{
		if (!This || !lpDDSurfaceDesc || !lplpDDSurface)
		{
			return s_origVtable.CreateSurface(This, lpDDSurfaceDesc, lplpDDSurface, pUnkOuter);
		}

		if (lpDDSurfaceDesc->ddsCaps.dwCaps & DDSCAPS_PRIMARYSURFACE)
		{
			return PrimarySurface::create<TDirectDraw>(*This, *lpDDSurfaceDesc, *lplpDDSurface);
		}
		else
		{
			return Surface::create<TDirectDraw>(*This, *lpDDSurfaceDesc, *lplpDDSurface, std::make_unique<Surface>());
		}
	}

	template <typename TDirectDraw>
	HRESULT STDMETHODCALLTYPE DirectDraw<TDirectDraw>::FlipToGDISurface(TDirectDraw* /*This*/)
	{
		return PrimarySurface::flipToGdiSurface();
	}

	template <typename TDirectDraw>
	HRESULT STDMETHODCALLTYPE DirectDraw<TDirectDraw>::GetGDISurface(
		TDirectDraw* /*This*/, TSurface** lplpGDIDDSSurface)
	{
		if (!lplpGDIDDSSurface)
		{
			return DDERR_INVALIDPARAMS;
		}

		auto gdiSurface(PrimarySurface::getGdiSurface());
		if (!gdiSurface)
		{
			return DDERR_NOTFOUND;
		}

		*lplpGDIDDSSurface = CompatPtr<TSurface>::from(gdiSurface.get()).detach();
		return DD_OK;
	}

	template <typename TDirectDraw>
	HRESULT STDMETHODCALLTYPE DirectDraw<TDirectDraw>::Initialize(TDirectDraw* This, GUID* lpGUID)
	{
		logComInstantiation();
		suppressEmulatedDirectDraw(lpGUID);
		return s_origVtable.Initialize(This, lpGUID);
	}

	template <typename TDirectDraw>
	HRESULT STDMETHODCALLTYPE DirectDraw<TDirectDraw>::WaitForVerticalBlank(
		TDirectDraw* This, DWORD dwFlags, HANDLE hEvent)
	{
		if (!This || (DDWAITVB_BLOCKBEGIN != dwFlags && DDWAITVB_BLOCKEND != dwFlags))
		{
			return s_origVtable.WaitForVerticalBlank(This, dwFlags, hEvent);
		}

		DWORD scanLine = 0;
		if (DDERR_VERTICALBLANKINPROGRESS != s_origVtable.GetScanLine(This, &scanLine))
		{
			D3dDdi::KernelModeThunks::waitForVerticalBlank();
		}

		if (DDWAITVB_BLOCKEND == dwFlags)
		{
			while (DDERR_VERTICALBLANKINPROGRESS == s_origVtable.GetScanLine(This, &scanLine));
		}

		return DD_OK;
	}


	template DirectDraw<IDirectDraw>;
	template DirectDraw<IDirectDraw2>;
	template DirectDraw<IDirectDraw4>;
	template DirectDraw<IDirectDraw7>;
}
