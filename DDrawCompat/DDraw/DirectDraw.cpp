#include "Common/CompatPtr.h"
#include "Common/CompatRef.h"
#include "DDraw/ActivateAppHandler.h"
#include "DDraw/DirectDraw.h"
#include "DDraw/DirectDrawSurface.h"
#include "DDraw/DisplayMode.h"
#include "DDraw/IReleaseNotifier.h"
#include "DDraw/Surfaces/Surface.h"
#include "DDraw/Surfaces/SurfaceImpl.h"

namespace
{
	struct DirectDrawInterface
	{
		void* vtable;
		void* ddObject;
		DirectDrawInterface* next;
		DWORD refCount;
		DWORD unknown1;
		DWORD unknown2;
	};

	DirectDrawInterface* g_fullScreenDirectDraw = nullptr;
	CompatWeakPtr<IDirectDrawSurface> g_fullScreenTagSurface;

	void onReleaseFullScreenTagSurface();

	DDraw::IReleaseNotifier g_fullScreenTagSurfaceReleaseNotifier(&onReleaseFullScreenTagSurface);

	CompatPtr<IDirectDrawSurface> createFullScreenTagSurface(CompatRef<IDirectDraw> dd)
	{
		DDSURFACEDESC desc = {};
		desc.dwSize = sizeof(desc);
		desc.dwFlags = DDSD_WIDTH | DDSD_HEIGHT | DDSD_CAPS;
		desc.dwWidth = 1;
		desc.dwHeight = 1;
		desc.ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN | DDSCAPS_SYSTEMMEMORY;

		CompatPtr<IDirectDrawSurface> tagSurface;
		dd->CreateSurface(&dd, &desc, &tagSurface.getRef(), nullptr);
		if (tagSurface)
		{
			CompatPtr<IDirectDrawSurface7> tagSurface7(tagSurface);
			tagSurface7->SetPrivateData(
				tagSurface7, IID_IReleaseNotifier, &g_fullScreenTagSurfaceReleaseNotifier,
				sizeof(&g_fullScreenTagSurfaceReleaseNotifier), DDSPD_IUNKNOWNPOINTER);
		}

		return tagSurface;
	}

	bool isFullScreenDirectDraw(void* dd)
	{
		return dd && g_fullScreenDirectDraw &&
			static_cast<DirectDrawInterface*>(dd)->ddObject == g_fullScreenDirectDraw->ddObject;
	}

	void onReleaseFullScreenTagSurface()
	{
		DDraw::ActivateAppHandler::setFullScreenCooperativeLevel(nullptr, nullptr, 0);
		g_fullScreenDirectDraw = nullptr;
		g_fullScreenTagSurface = nullptr;
	}

	void setFullScreenDirectDraw(CompatRef<IDirectDraw> dd)
	{
		g_fullScreenTagSurface.release();
		g_fullScreenTagSurface = createFullScreenTagSurface(dd).detach();

		/*
		IDirectDraw interfaces don't conform to the COM rule about object identity:
		QueryInterface with IID_IUnknown does not always return the same pointer for the same object.
		The IUnknown (== IDirectDraw v1) interface may even be freed, making the interface invalid,
		while the DirectDraw object itself can still be kept alive by its other interfaces.
		Unfortunately, the IDirectDrawSurface GetDDInterface method inherits this problem and may
		also return an invalid (already freed) interface pointer.
		To work around this problem, a copy of the necessary interface data is passed
		to CompatActivateAppHandler, which is sufficient for it to use QueryInterface to "safely"
		obtain a valid interface pointer (other than IUnknown/IDirectDraw v1) to the full-screen
		DirectDraw object.
		*/

		static DirectDrawInterface fullScreenDirectDraw = {};
		ZeroMemory(&fullScreenDirectDraw, sizeof(fullScreenDirectDraw));
		DirectDrawInterface& ddIntf = reinterpret_cast<DirectDrawInterface&>(dd.get());
		fullScreenDirectDraw.vtable = ddIntf.vtable;
		fullScreenDirectDraw.ddObject = ddIntf.ddObject;
		g_fullScreenDirectDraw = &fullScreenDirectDraw;
	}
}

namespace DDraw
{
	template <typename TDirectDraw>
	void DirectDraw<TDirectDraw>::setCompatVtable(Vtable<TDirectDraw>& vtable)
	{
		vtable.CreateSurface = &CreateSurface;
		vtable.GetDisplayMode = &GetDisplayMode;
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
			result = SurfaceImpl<TSurface>::createCompatPrimarySurface<TDirectDraw>(
				*This, *lpDDSurfaceDesc, *lplpDDSurface);
		}
		else
		{
			result = Surface::create<TDirectDraw>(*This, *lpDDSurfaceDesc, *lplpDDSurface);
		}

		return result;
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
			if (dwFlags & DDSCL_FULLSCREEN)
			{
				CompatPtr<IDirectDraw> dd(Compat::queryInterface<IDirectDraw>(This));
				setFullScreenDirectDraw(*dd);
				ActivateAppHandler::setFullScreenCooperativeLevel(
					reinterpret_cast<IUnknown*>(g_fullScreenDirectDraw), hWnd, dwFlags);
			}
			else if (isFullScreenDirectDraw(This) && g_fullScreenTagSurface)
			{
				g_fullScreenTagSurface.release();
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
