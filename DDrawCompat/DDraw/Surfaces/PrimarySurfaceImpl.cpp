#include "DDraw/DirectDrawPalette.h"
#include "DDraw/RealPrimarySurface.h"
#include "DDraw/Surfaces/PrimarySurface.h"
#include "DDraw/Surfaces/PrimarySurfaceImpl.h"
#include "Gdi/Gdi.h"

namespace
{
	void restorePrimaryCaps(DWORD& caps)
	{
		caps &= ~DDSCAPS_OFFSCREENPLAIN;
		caps |= DDSCAPS_PRIMARYSURFACE | DDSCAPS_VISIBLE;
	}
}

namespace DDraw
{
	template <typename TSurface>
	HRESULT PrimarySurfaceImpl<TSurface>::Blt(
		TSurface* This, LPRECT lpDestRect, TSurface* lpDDSrcSurface, LPRECT lpSrcRect,
		DWORD dwFlags, LPDDBLTFX lpDDBltFx)
	{
		if (RealPrimarySurface::isLost())
		{
			return DDERR_SURFACELOST;
		}

		HRESULT result = SurfaceImpl::Blt(This, lpDestRect, lpDDSrcSurface, lpSrcRect, dwFlags, lpDDBltFx);
		if (SUCCEEDED(result))
		{
			RealPrimarySurface::invalidate(lpDestRect);
			RealPrimarySurface::update();
		}
		return result;
	}

	template <typename TSurface>
	HRESULT PrimarySurfaceImpl<TSurface>::BltFast(
		TSurface* This, DWORD dwX, DWORD dwY, TSurface* lpDDSrcSurface, LPRECT lpSrcRect, DWORD dwTrans)
	{
		if (RealPrimarySurface::isLost())
		{
			return DDERR_SURFACELOST;
		}

		HRESULT result = SurfaceImpl::BltFast(This, dwX, dwY, lpDDSrcSurface, lpSrcRect, dwTrans);
		if (SUCCEEDED(result))
		{
			const LONG x = dwX;
			const LONG y = dwY;
			RECT destRect = { x, y, x, y };
			if (lpSrcRect)
			{
				destRect.right += lpSrcRect->right - lpSrcRect->left;
				destRect.bottom += lpSrcRect->bottom - lpSrcRect->top;
			}
			else
			{
				TSurfaceDesc desc = {};
				desc.dwSize = sizeof(desc);
				CompatVtableBase<TSurface>::s_origVtable.GetSurfaceDesc(lpDDSrcSurface, &desc);
				destRect.right += desc.dwWidth;
				destRect.bottom += desc.dwHeight;
			}
			RealPrimarySurface::invalidate(&destRect);
			RealPrimarySurface::update();
		}
		return result;
	}

	template <typename TSurface>
	HRESULT PrimarySurfaceImpl<TSurface>::Flip(TSurface* This, TSurface* lpDDSurfaceTargetOverride, DWORD dwFlags)
	{
		if (RealPrimarySurface::isLost())
		{
			return DDERR_SURFACELOST;
		}

		HRESULT result = SurfaceImpl::Flip(This, lpDDSurfaceTargetOverride, dwFlags);
		if (SUCCEEDED(result))
		{
			result = RealPrimarySurface::flip(dwFlags);
		}
		return result;
	}

	template <typename TSurface>
	HRESULT PrimarySurfaceImpl<TSurface>::GetCaps(TSurface* This, TDdsCaps* lpDDSCaps)
	{
		HRESULT result = SurfaceImpl::GetCaps(This, lpDDSCaps);
		if (SUCCEEDED(result))
		{
			restorePrimaryCaps(lpDDSCaps->dwCaps);
		}
		return result;
	}

	template <typename TSurface>
	HRESULT PrimarySurfaceImpl<TSurface>::GetSurfaceDesc(TSurface* This, TSurfaceDesc* lpDDSurfaceDesc)
	{
		HRESULT result = SurfaceImpl::GetSurfaceDesc(This, lpDDSurfaceDesc);
		if (SUCCEEDED(result))
		{
			restorePrimaryCaps(lpDDSurfaceDesc->ddsCaps.dwCaps);
		}
		return result;
	}

	template <typename TSurface>
	HRESULT PrimarySurfaceImpl<TSurface>::IsLost(TSurface* This)
	{
		HRESULT result = SurfaceImpl::IsLost(This);
		if (SUCCEEDED(result))
		{
			result = RealPrimarySurface::isLost() ? DDERR_SURFACELOST : DD_OK;
		}
		return result;
	}

	template <typename TSurface>
	HRESULT PrimarySurfaceImpl<TSurface>::Lock(
		TSurface* This, LPRECT lpDestRect, TSurfaceDesc* lpDDSurfaceDesc,
		DWORD dwFlags, HANDLE hEvent)
	{
		if (RealPrimarySurface::isLost())
		{
			return DDERR_SURFACELOST;
		}

		HRESULT result = SurfaceImpl::Lock(This, lpDestRect, lpDDSurfaceDesc, dwFlags, hEvent);
		if (SUCCEEDED(result))
		{
			RealPrimarySurface::invalidate(lpDestRect);
			restorePrimaryCaps(lpDDSurfaceDesc->ddsCaps.dwCaps);
		}
		return result;
	}

	template <typename TSurface>
	HRESULT PrimarySurfaceImpl<TSurface>::QueryInterface(TSurface* This, REFIID riid, LPVOID* obp)
	{
		if (riid == IID_IDirectDrawGammaControl)
		{
			auto realPrimary(RealPrimarySurface::getSurface());
			return realPrimary->QueryInterface(realPrimary, riid, obp);
		}
		return SurfaceImpl::QueryInterface(This, riid, obp);
	}

	template <typename TSurface>
	HRESULT PrimarySurfaceImpl<TSurface>::ReleaseDC(TSurface* This, HDC hDC)
	{
		HRESULT result = SurfaceImpl::ReleaseDC(This, hDC);
		if (SUCCEEDED(result))
		{
			RealPrimarySurface::invalidate(nullptr);
			RealPrimarySurface::update();
		}
		return result;
	}

	template <typename TSurface>
	HRESULT PrimarySurfaceImpl<TSurface>::Restore(TSurface* This)
	{
		HRESULT result = IsLost(This);
		if (FAILED(result))
		{
			result = RealPrimarySurface::restore();
			if (SUCCEEDED(result))
			{
				result = SurfaceImpl::Restore(This);
				if (SUCCEEDED(result))
				{
					fixSurfacePtrs(*This);
					Gdi::invalidate(nullptr);
				}
			}
		}
		return result;
	}

	template <typename TSurface>
	HRESULT PrimarySurfaceImpl<TSurface>::SetClipper(TSurface* This, LPDIRECTDRAWCLIPPER lpDDClipper)
	{
		HRESULT result = SurfaceImpl::SetClipper(This, lpDDClipper);
		if (SUCCEEDED(result))
		{
			RealPrimarySurface::setClipper(lpDDClipper);
		}
		return result;
	}

	template <typename TSurface>
	HRESULT PrimarySurfaceImpl<TSurface>::SetPalette(TSurface* This, LPDIRECTDRAWPALETTE lpDDPalette)
	{
		if (lpDDPalette)
		{
			DirectDrawPalette::waitForNextUpdate();
		}
		if (lpDDPalette == PrimarySurface::s_palette)
		{
			return DD_OK;
		}

		HRESULT result = SurfaceImpl::SetPalette(This, lpDDPalette);
		if (SUCCEEDED(result))
		{
			PrimarySurface::s_palette = lpDDPalette;
			RealPrimarySurface::setPalette();
		}
		return result;
	}

	template <typename TSurface>
	HRESULT PrimarySurfaceImpl<TSurface>::Unlock(TSurface* This, TUnlockParam lpRect)
	{
		HRESULT result = SurfaceImpl::Unlock(This, lpRect);
		if (SUCCEEDED(result))
		{
			RealPrimarySurface::update();
		}
		return result;
	}

	template PrimarySurfaceImpl<IDirectDrawSurface>;
	template PrimarySurfaceImpl<IDirectDrawSurface2>;
	template PrimarySurfaceImpl<IDirectDrawSurface3>;
	template PrimarySurfaceImpl<IDirectDrawSurface4>;
	template PrimarySurfaceImpl<IDirectDrawSurface7>;
}
