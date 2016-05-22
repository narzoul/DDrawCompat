#include "CompatActivateAppHandler.h"
#include "CompatDirectDraw.h"
#include "CompatDirectDrawSurface.h"
#include "CompatDisplayMode.h"
#include "CompatPtr.h"
#include "CompatRef.h"
#include "IReleaseNotifier.h"

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

	IReleaseNotifier g_fullScreenTagSurfaceReleaseNotifier(&onReleaseFullScreenTagSurface);

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
		CompatActivateAppHandler::setFullScreenCooperativeLevel(nullptr, nullptr, 0);
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

template <typename TDirectDraw>
void CompatDirectDraw<TDirectDraw>::setCompatVtable(Vtable<TDirectDraw>& vtable)
{
	vtable.CreateSurface = &CreateSurface;
	vtable.RestoreDisplayMode = &RestoreDisplayMode;
	vtable.SetCooperativeLevel = &SetCooperativeLevel;
	vtable.SetDisplayMode = &SetDisplayMode;
}

template <typename TDirectDraw>
HRESULT STDMETHODCALLTYPE CompatDirectDraw<TDirectDraw>::CreateSurface(
	TDirectDraw* This,
	TSurfaceDesc* lpDDSurfaceDesc,
	TSurface** lplpDDSurface,
	IUnknown* pUnkOuter)
{
	HRESULT result = DD_OK;

	const bool isPrimary = lpDDSurfaceDesc &&
		(lpDDSurfaceDesc->dwFlags & DDSD_CAPS) &&
		(lpDDSurfaceDesc->ddsCaps.dwCaps & DDSCAPS_PRIMARYSURFACE);

	if (isPrimary)
	{
		result = CompatDirectDrawSurface<TSurface>::createCompatPrimarySurface<TDirectDraw>(
			*This, *lpDDSurfaceDesc, *lplpDDSurface);
	}
	else
	{
		if (lpDDSurfaceDesc &&
			!(lpDDSurfaceDesc->dwFlags & DDSD_PIXELFORMAT) &&
			(lpDDSurfaceDesc->dwFlags & DDSD_WIDTH) &&
			(lpDDSurfaceDesc->dwFlags & DDSD_HEIGHT) &&
			!((lpDDSurfaceDesc->dwFlags & DDSD_CAPS) &&
				(lpDDSurfaceDesc->ddsCaps.dwCaps & (DDSCAPS_ALPHA | DDSCAPS_ZBUFFER))))
		{
			CompatPtr<IDirectDraw7> dd(Compat::queryInterface<IDirectDraw7>(This));
			auto dm = CompatDisplayMode::getDisplayMode(*dd);

			TSurfaceDesc desc = *lpDDSurfaceDesc;
			desc.dwFlags |= DDSD_PIXELFORMAT;
			desc.ddpfPixelFormat = dm.pixelFormat;
			result = s_origVtable.CreateSurface(This, &desc, lplpDDSurface, pUnkOuter);
		}
		else
		{
			result = s_origVtable.CreateSurface(This, lpDDSurfaceDesc, lplpDDSurface, pUnkOuter);
		}
	}

	if (SUCCEEDED(result))
	{
		CompatDirectDrawSurface<TSurface>::fixSurfacePtrs(**lplpDDSurface);
	}

	return result;
}

template <typename TDirectDraw>
HRESULT STDMETHODCALLTYPE CompatDirectDraw<TDirectDraw>::RestoreDisplayMode(TDirectDraw* This)
{
	CompatPtr<IDirectDraw7> dd(Compat::queryInterface<IDirectDraw7>(This));
	return CompatDisplayMode::restoreDisplayMode(*dd);
}

template <typename TDirectDraw>
HRESULT STDMETHODCALLTYPE CompatDirectDraw<TDirectDraw>::SetCooperativeLevel(
	TDirectDraw* This, HWND hWnd, DWORD dwFlags)
{
	if ((dwFlags & DDSCL_FULLSCREEN) && !CompatActivateAppHandler::isActive())
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
			CompatActivateAppHandler::setFullScreenCooperativeLevel(
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
HRESULT STDMETHODCALLTYPE CompatDirectDraw<TDirectDraw>::SetDisplayMode(
	TDirectDraw* This,
	DWORD dwWidth,
	DWORD dwHeight,
	DWORD dwBPP,
	Params... params)
{
	CompatPtr<IDirectDraw7> dd(Compat::queryInterface<IDirectDraw7>(This));
	return CompatDisplayMode::setDisplayMode(*dd, dwWidth, dwHeight, dwBPP, params...);
}

template <> const IID& CompatDirectDraw<IDirectDraw>::s_iid = IID_IDirectDraw;
template <> const IID& CompatDirectDraw<IDirectDraw2>::s_iid = IID_IDirectDraw2;
template <> const IID& CompatDirectDraw<IDirectDraw4>::s_iid = IID_IDirectDraw4;
template <> const IID& CompatDirectDraw<IDirectDraw7>::s_iid = IID_IDirectDraw7;

template CompatDirectDraw<IDirectDraw>;
template CompatDirectDraw<IDirectDraw2>;
template CompatDirectDraw<IDirectDraw4>;
template CompatDirectDraw<IDirectDraw7>;
