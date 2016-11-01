#include "Common/CompatPtr.h"
#include "Common/CompatRef.h"
#include "DDraw/ActivateAppHandler.h"
#include "DDraw/DirectDraw.h"
#include "DDraw/DirectDrawSurface.h"
#include "DDraw/DisplayMode.h"
#include "DDraw/Surfaces/TagSurface.h"
#include "DDraw/Surfaces/PrimarySurface.h"
#include "DDraw/Surfaces/Surface.h"

namespace DDraw
{
	TagSurface* g_fullScreenTagSurface = nullptr;

	template <typename TDirectDraw>
	void* getDdObject(TDirectDraw& dd)
	{
		return reinterpret_cast<void**>(&dd)[1];
	}

	template void* getDdObject(IDirectDraw&);
	template void* getDdObject(IDirectDraw2&);
	template void* getDdObject(IDirectDraw4&);
	template void* getDdObject(IDirectDraw7&);

	CompatPtr<IDirectDraw7> getFullScreenDirectDraw()
	{
		return g_fullScreenTagSurface ? g_fullScreenTagSurface->getDirectDraw() : nullptr;
	}

	void onRelease(TagSurface& dd)
	{
		if (&dd == g_fullScreenTagSurface)
		{
			g_fullScreenTagSurface = nullptr;
		}
	}

	template <typename TDirectDraw>
	void DirectDraw<TDirectDraw>::setCompatVtable(Vtable<TDirectDraw>& vtable)
	{
		vtable.CreateSurface = &CreateSurface;
		vtable.FlipToGDISurface = &FlipToGDISurface;
		vtable.GetDisplayMode = &GetDisplayMode;
		vtable.GetGDISurface = &GetGDISurface;
		vtable.RestoreDisplayMode = &RestoreDisplayMode;
		vtable.SetCooperativeLevel = &SetCooperativeLevel;
		vtable.SetDisplayMode = &SetDisplayMode;
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

		HRESULT result = DD_OK;

		const bool isPrimary = 0 != (lpDDSurfaceDesc->ddsCaps.dwCaps & DDSCAPS_PRIMARYSURFACE);

		if (isPrimary)
		{
			result = PrimarySurface::create<TDirectDraw>(*This, *lpDDSurfaceDesc, *lplpDDSurface);
		}
		else
		{
			result = Surface::create<TDirectDraw>(*This, *lpDDSurfaceDesc, *lplpDDSurface);
		}

		return result;
	}

	template <typename TDirectDraw>
	HRESULT STDMETHODCALLTYPE DirectDraw<TDirectDraw>::FlipToGDISurface(TDirectDraw* /*This*/)
	{
		return PrimarySurface::flipToGdiSurface();
	}

	template <typename TDirectDraw>
	HRESULT STDMETHODCALLTYPE DirectDraw<TDirectDraw>::GetDisplayMode(
		TDirectDraw* This, TSurfaceDesc* lpDDSurfaceDesc)
	{
		const DWORD size = lpDDSurfaceDesc ? lpDDSurfaceDesc->dwSize : 0;
		if (sizeof(DDSURFACEDESC) != size && sizeof(DDSURFACEDESC2) != size)
		{
			return DDERR_INVALIDPARAMS;
		}

		CompatPtr<IDirectDraw7> dd(Compat::queryInterface<IDirectDraw7>(This));
		const DDSURFACEDESC2 dm = DisplayMode::getDisplayMode(*dd);
		CopyMemory(lpDDSurfaceDesc, &dm, size);
		lpDDSurfaceDesc->dwSize = size;

		return DD_OK;
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
	HRESULT STDMETHODCALLTYPE DirectDraw<TDirectDraw>::RestoreDisplayMode(TDirectDraw* This)
	{
		CompatPtr<IDirectDraw7> dd(Compat::queryInterface<IDirectDraw7>(This));
		return DisplayMode::restoreDisplayMode(*dd);
	}

	template <typename TDirectDraw>
	HRESULT STDMETHODCALLTYPE DirectDraw<TDirectDraw>::SetCooperativeLevel(
		TDirectDraw* This, HWND hWnd, DWORD dwFlags)
	{
		if ((dwFlags & DDSCL_FULLSCREEN) && !ActivateAppHandler::isActive())
		{
			return DDERR_EXCLUSIVEMODEALREADYSET;
		}

		HRESULT result = s_origVtable.SetCooperativeLevel(This, hWnd, dwFlags);
		if (SUCCEEDED(result))
		{
			void* ddObject = getDdObject(*This);
			TagSurface* tagSurface = TagSurface::get(ddObject);
			if (!tagSurface)
			{
				CompatPtr<IDirectDraw> dd(Compat::queryInterface<IDirectDraw>(This));
				TagSurface::create(*dd);
				tagSurface = TagSurface::get(ddObject);
			}

			if (tagSurface)
			{
				if (dwFlags & DDSCL_FULLSCREEN)
				{
					g_fullScreenTagSurface = tagSurface;
					ActivateAppHandler::setFullScreenCooperativeLevel(hWnd, dwFlags);
				}
				else if ((dwFlags & DDSCL_NORMAL) && tagSurface == g_fullScreenTagSurface)
				{
					g_fullScreenTagSurface = nullptr;
				}
			}
		}
		return result;
	}

	template <typename TDirectDraw>
	template <typename... Params>
	HRESULT STDMETHODCALLTYPE DirectDraw<TDirectDraw>::SetDisplayMode(
		TDirectDraw* This,
		DWORD dwWidth,
		DWORD dwHeight,
		DWORD dwBPP,
		Params... params)
	{
		CompatPtr<IDirectDraw7> dd(Compat::queryInterface<IDirectDraw7>(This));
		return DisplayMode::setDisplayMode(*dd, dwWidth, dwHeight, dwBPP, params...);
	}

	template DirectDraw<IDirectDraw>;
	template DirectDraw<IDirectDraw2>;
	template DirectDraw<IDirectDraw4>;
	template DirectDraw<IDirectDraw7>;
}
