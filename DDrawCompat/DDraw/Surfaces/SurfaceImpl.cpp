#include <set>

#include "Common/CompatPtr.h"
#include "DDraw/CompatPrimarySurface.h"
#include "DDraw/DirectDrawPalette.h"
#include "DDraw/DisplayMode.h"
#include "DDraw/RealPrimarySurface.h"
#include "DDraw/Repository.h"
#include "DDraw/Surfaces/Surface.h"
#include "DDraw/Surfaces/SurfaceImpl.h"
#include "Gdi/Gdi.h"

namespace
{
	bool mirrorBlt(CompatRef<IDirectDrawSurface7> dst, CompatRef<IDirectDrawSurface7> src,
		RECT srcRect, DWORD mirrorFx);

	bool g_lockingPrimary = false;

	void fixSurfacePtr(CompatRef<IDirectDrawSurface7> surface, const DDSURFACEDESC2& desc)
	{
		if ((desc.ddsCaps.dwCaps & DDSCAPS_SYSTEMMEMORY) || 0 == desc.dwWidth || 0 == desc.dwHeight)
		{
			return;
		}

		DDSURFACEDESC2 tempSurfaceDesc = desc;
		tempSurfaceDesc.dwWidth = 1;
		tempSurfaceDesc.dwHeight = 1;
		DDraw::Repository::ScopedSurface tempSurface(desc);
		if (!tempSurface.surface)
		{
			return;
		}

		RECT r = { 0, 0, 1, 1 };
		surface->Blt(&surface, &r, tempSurface.surface, &r, DDBLT_WAIT, nullptr);
	}

	HRESULT WINAPI fixSurfacePtrEnumCallback(
		LPDIRECTDRAWSURFACE7 lpDDSurface,
		LPDDSURFACEDESC2 lpDDSurfaceDesc,
		LPVOID lpContext)
	{
		auto& visitedSurfaces = *static_cast<std::set<IDirectDrawSurface7*>*>(lpContext);

		CompatPtr<IDirectDrawSurface7> surface(lpDDSurface);
		if (visitedSurfaces.find(surface) == visitedSurfaces.end())
		{
			visitedSurfaces.insert(surface);
			fixSurfacePtr(*surface, *lpDDSurfaceDesc);
			surface->EnumAttachedSurfaces(surface, lpContext, &fixSurfacePtrEnumCallback);
		}

		return DDENUMRET_OK;
	}

	void fixSurfacePtrs(CompatRef<IDirectDrawSurface7> surface)
	{
		DDSURFACEDESC2 desc = {};
		desc.dwSize = sizeof(desc);
		surface->GetSurfaceDesc(&surface, &desc);

		fixSurfacePtr(surface, desc);
		std::set<IDirectDrawSurface7*> visitedSurfaces{ &surface };
		surface->EnumAttachedSurfaces(&surface, &visitedSurfaces, &fixSurfacePtrEnumCallback);
	}

	CompatWeakPtr<IDirectDrawSurface7> getMirroredSurface(
		CompatRef<IDirectDrawSurface7> surface, RECT* srcRect, DWORD mirrorFx)
	{
		DDSURFACEDESC2 desc = {};
		desc.dwSize = sizeof(desc);
		HRESULT result = surface->GetSurfaceDesc(&surface, &desc);
		if (FAILED(result))
		{
			LOG_ONCE("Failed to get surface description for mirroring: " << result);
			return nullptr;
		}

		if (srcRect)
		{
			desc.dwWidth = srcRect->right - srcRect->left;
			desc.dwHeight = srcRect->bottom - srcRect->top;
		}

		DDraw::Repository::ScopedSurface mirroredSurface(desc);
		if (!mirroredSurface.surface)
		{
			return nullptr;
		}

		RECT rect = { 0, 0, static_cast<LONG>(desc.dwWidth), static_cast<LONG>(desc.dwHeight) };
		if ((mirrorFx & DDBLTFX_MIRRORLEFTRIGHT) && (mirrorFx & DDBLTFX_MIRRORUPDOWN))
		{
			DDraw::Repository::Surface tempMirroredSurface = DDraw::Repository::ScopedSurface(desc);
			if (!tempMirroredSurface.surface ||
				!mirrorBlt(*tempMirroredSurface.surface, surface, srcRect ? *srcRect : rect,
					DDBLTFX_MIRRORLEFTRIGHT) ||
				!mirrorBlt(*mirroredSurface.surface, *tempMirroredSurface.surface, rect,
					DDBLTFX_MIRRORUPDOWN))
			{
				return nullptr;
			}
		}
		else if (!mirrorBlt(*mirroredSurface.surface, surface, srcRect ? *srcRect : rect, mirrorFx))
		{
			return nullptr;
		}

		return mirroredSurface.surface;
	}

	bool mirrorBlt(CompatRef<IDirectDrawSurface7> dst, CompatRef<IDirectDrawSurface7> src,
		RECT srcRect, DWORD mirrorFx)
	{
		if (DDBLTFX_MIRRORLEFTRIGHT == mirrorFx)
		{
			LONG width = srcRect.right - srcRect.left;
			srcRect.left = srcRect.right - 1;
			for (LONG x = 0; x < width; ++x)
			{
				HRESULT result = dst->BltFast(&dst, x, 0, &src, &srcRect, DDBLTFAST_WAIT);
				if (FAILED(result))
				{
					LOG_ONCE("Failed BltFast for mirroring: " << result);
					return false;
				}
				--srcRect.left;
				--srcRect.right;
			}
		}
		else
		{
			LONG height = srcRect.bottom - srcRect.top;
			srcRect.top = srcRect.bottom - 1;
			for (LONG y = 0; y < height; ++y)
			{
				HRESULT result = dst->BltFast(&dst, 0, y, &src, &srcRect, DDBLTFAST_WAIT);
				if (FAILED(result))
				{
					LOG_ONCE("Failed BltFast for mirroring: " << result);
					return false;
				}
				--srcRect.top;
				--srcRect.bottom;
			}
		}

		return true;
	}
}

namespace DDraw
{
	template <typename TSurface>
	template <typename TDirectDraw>
	HRESULT SurfaceImpl<TSurface>::createCompatPrimarySurface(
		CompatRef<TDirectDraw> dd,
		TSurfaceDesc compatDesc,
		TSurface*& compatSurface)
	{
		HRESULT result = RealPrimarySurface::create(dd);
		if (FAILED(result))
		{
			return result;
		}

		CompatPtr<IDirectDraw7> dd7(Compat::queryInterface<IDirectDraw7>(&dd));
		const auto& dm = DisplayMode::getDisplayMode(*dd7);
		compatDesc.dwFlags |= DDSD_WIDTH | DDSD_HEIGHT | DDSD_PIXELFORMAT;
		compatDesc.dwWidth = dm.dwWidth;
		compatDesc.dwHeight = dm.dwHeight;
		compatDesc.ddsCaps.dwCaps &= ~DDSCAPS_PRIMARYSURFACE;
		compatDesc.ddsCaps.dwCaps |= DDSCAPS_OFFSCREENPLAIN;
		compatDesc.ddpfPixelFormat = dm.ddpfPixelFormat;

		result = Surface::create(dd, compatDesc, compatSurface);
		if (FAILED(result))
		{
			Compat::Log() << "Failed to create the compat primary surface!";
			RealPrimarySurface::release();
			return result;
		}

		CompatPtr<IDirectDrawSurface7> primary(Compat::queryInterface<IDirectDrawSurface7>(compatSurface));
		CompatPrimarySurface::setPrimary(*primary);

		return DD_OK;
	}

	template <typename TSurface>
	SurfaceImpl<TSurface>::~SurfaceImpl()
	{
	}

	template <typename TSurface>
	void SurfaceImpl<TSurface>::fixSurfacePtrs(CompatRef<TSurface> surface)
	{
		CompatPtr<IDirectDrawSurface7> surface7(Compat::queryInterface<IDirectDrawSurface7>(&surface));
		::fixSurfacePtrs(*surface7);
	}

	template <typename TSurface>
	HRESULT SurfaceImpl<TSurface>::Blt(
		TSurface* This, LPRECT lpDestRect, TSurface* lpDDSrcSurface, LPRECT lpSrcRect,
		DWORD dwFlags, LPDDBLTFX lpDDBltFx)
	{
		const bool isPrimaryDest = CompatPrimarySurface::isPrimary(This);
		if ((isPrimaryDest || CompatPrimarySurface::isPrimary(lpDDSrcSurface)) &&
			RealPrimarySurface::isLost())
		{
			return DDERR_SURFACELOST;
		}

		HRESULT result = DD_OK;
		CompatPtr<TSurface> mirroredSrcSurface;

		if (lpDDSrcSurface && (dwFlags & DDBLT_DDFX) && lpDDBltFx &&
			(lpDDBltFx->dwDDFX & (DDBLTFX_MIRRORLEFTRIGHT | DDBLTFX_MIRRORUPDOWN)))
		{
			CompatPtr<IDirectDrawSurface7> srcSurface(
				Compat::queryInterface<IDirectDrawSurface7>(lpDDSrcSurface));
			mirroredSrcSurface.reset(Compat::queryInterface<TSurface>(
				getMirroredSurface(*srcSurface, lpSrcRect, lpDDBltFx->dwDDFX).get()));
			if (!mirroredSrcSurface)
			{
				LOG_ONCE("Failed to emulate a mirrored Blt");
			}
		}

		if (mirroredSrcSurface)
		{
			DWORD flags = dwFlags;
			DDBLTFX fx = *lpDDBltFx;
			fx.dwDDFX &= ~(DDBLTFX_MIRRORLEFTRIGHT | DDBLTFX_MIRRORUPDOWN);
			if (0 == fx.dwDDFX)
			{
				flags ^= DDBLT_DDFX;
			}
			if (flags & DDBLT_KEYSRC)
			{
				DDCOLORKEY srcColorKey = {};
				s_origVtable.GetColorKey(lpDDSrcSurface, DDCKEY_SRCBLT, &srcColorKey);
				s_origVtable.SetColorKey(mirroredSrcSurface, DDCKEY_SRCBLT, &srcColorKey);
			}

			if (lpSrcRect)
			{
				RECT srcRect = {
					0, 0, lpSrcRect->right - lpSrcRect->left, lpSrcRect->bottom - lpSrcRect->top };
				result = s_origVtable.Blt(This, lpDestRect, mirroredSrcSurface, &srcRect, flags, &fx);
			}
			else
			{
				result = s_origVtable.Blt(This, lpDestRect, mirroredSrcSurface, nullptr, flags, &fx);
			}
		}
		else
		{
			result = s_origVtable.Blt(This, lpDestRect, lpDDSrcSurface, lpSrcRect, dwFlags, lpDDBltFx);
		}

		if (isPrimaryDest && SUCCEEDED(result))
		{
			RealPrimarySurface::invalidate(lpDestRect);
			RealPrimarySurface::update();
		}

		return result;
	}

	template <typename TSurface>
	HRESULT SurfaceImpl<TSurface>::BltFast(
		TSurface* This, DWORD dwX, DWORD dwY, TSurface* lpDDSrcSurface, LPRECT lpSrcRect, DWORD dwTrans)
	{
		const bool isPrimaryDest = CompatPrimarySurface::isPrimary(This);
		if ((isPrimaryDest || CompatPrimarySurface::isPrimary(lpDDSrcSurface)) &&
			RealPrimarySurface::isLost())
		{
			return DDERR_SURFACELOST;
		}

		HRESULT result = s_origVtable.BltFast(This, dwX, dwY, lpDDSrcSurface, lpSrcRect, dwTrans);
		if (isPrimaryDest && SUCCEEDED(result))
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
				s_origVtable.GetSurfaceDesc(lpDDSrcSurface, &desc);
				destRect.right += desc.dwWidth;
				destRect.bottom += desc.dwHeight;
			}
			RealPrimarySurface::invalidate(&destRect);
			RealPrimarySurface::update();
		}
		return result;
	}

	template <typename TSurface>
	HRESULT SurfaceImpl<TSurface>::Flip(TSurface* This, TSurface* lpDDSurfaceTargetOverride, DWORD dwFlags)
	{
		HRESULT result = s_origVtable.Flip(This, lpDDSurfaceTargetOverride, dwFlags);
		if (SUCCEEDED(result) && CompatPrimarySurface::isPrimary(This))
		{
			result = RealPrimarySurface::flip(dwFlags);
		}
		return result;
	}

	template <typename TSurface>
	HRESULT SurfaceImpl<TSurface>::GetCaps(TSurface* This, TDdsCaps* lpDDSCaps)
	{
		HRESULT result = s_origVtable.GetCaps(This, lpDDSCaps);
		if (SUCCEEDED(result) && CompatPrimarySurface::isPrimary(This))
		{
			restorePrimaryCaps(*lpDDSCaps);
		}
		return result;
	}

	template <typename TSurface>
	HRESULT SurfaceImpl<TSurface>::GetSurfaceDesc(TSurface* This, TSurfaceDesc* lpDDSurfaceDesc)
	{
		HRESULT result = s_origVtable.GetSurfaceDesc(This, lpDDSurfaceDesc);
		if (SUCCEEDED(result) && !g_lockingPrimary && CompatPrimarySurface::isPrimary(This))
		{
			restorePrimaryCaps(lpDDSurfaceDesc->ddsCaps);
		}
		return result;
	}

	template <typename TSurface>
	HRESULT SurfaceImpl<TSurface>::IsLost(TSurface* This)
	{
		HRESULT result = s_origVtable.IsLost(This);
		if (SUCCEEDED(result) && CompatPrimarySurface::isPrimary(This))
		{
			result = RealPrimarySurface::isLost() ? DDERR_SURFACELOST : DD_OK;
		}
		return result;
	}

	template <typename TSurface>
	HRESULT SurfaceImpl<TSurface>::Lock(
		TSurface* This, LPRECT lpDestRect, TSurfaceDesc* lpDDSurfaceDesc,
		DWORD dwFlags, HANDLE hEvent)
	{
		if (CompatPrimarySurface::isPrimary(This))
		{
			if (RealPrimarySurface::isLost())
			{
				return DDERR_SURFACELOST;
			}
			g_lockingPrimary = true;
		}

		HRESULT result = s_origVtable.Lock(This, lpDestRect, lpDDSurfaceDesc, dwFlags, hEvent);
		if (SUCCEEDED(result) && g_lockingPrimary && lpDDSurfaceDesc)
		{
			RealPrimarySurface::invalidate(lpDestRect);
			restorePrimaryCaps(lpDDSurfaceDesc->ddsCaps);
		}
		else if (DDERR_SURFACELOST == result)
		{
			TSurfaceDesc desc = {};
			desc.dwSize = sizeof(desc);
			if (SUCCEEDED(s_origVtable.GetSurfaceDesc(This, &desc)) && !(desc.dwFlags & DDSD_HEIGHT))
			{
				// Fixes missing handling for lost vertex buffers in Messiah
				s_origVtable.Restore(This);
				// Still, pass back DDERR_SURFACELOST to the application in case it handles it
			}
		}

		g_lockingPrimary = false;
		return result;
	}

	template <typename TSurface>
	HRESULT SurfaceImpl<TSurface>::QueryInterface(TSurface* This, REFIID riid, LPVOID* obp)
	{
		if (riid == IID_IDirectDrawGammaControl && CompatPrimarySurface::isPrimary(This))
		{
			auto realPrimary(RealPrimarySurface::getSurface());
			return realPrimary->QueryInterface(realPrimary, riid, obp);
		}
		return s_origVtable.QueryInterface(This, riid, obp);
	}

	template <typename TSurface>
	HRESULT SurfaceImpl<TSurface>::ReleaseDC(TSurface* This, HDC hDC)
	{
		const bool isPrimary = CompatPrimarySurface::isPrimary(This);
		if (isPrimary && RealPrimarySurface::isLost())
		{
			return DDERR_SURFACELOST;
		}

		HRESULT result = s_origVtable.ReleaseDC(This, hDC);
		if (isPrimary && SUCCEEDED(result))
		{
			RealPrimarySurface::invalidate(nullptr);
			RealPrimarySurface::update();
		}
		return result;
	}

	template <typename TSurface>
	HRESULT SurfaceImpl<TSurface>::Restore(TSurface* This)
	{
		const bool wasLost = DDERR_SURFACELOST == s_origVtable.IsLost(This);
		HRESULT result = s_origVtable.Restore(This);
		if (SUCCEEDED(result))
		{
			if (wasLost)
			{
				fixSurfacePtrs(*This);
			}
			if (CompatPrimarySurface::isPrimary(This))
			{
				result = RealPrimarySurface::restore();
				if (wasLost)
				{
					Gdi::invalidate(nullptr);
				}
			}
		}
		return result;
	}

	template <typename TSurface>
	HRESULT SurfaceImpl<TSurface>::SetClipper(TSurface* This, LPDIRECTDRAWCLIPPER lpDDClipper)
	{
		HRESULT result = s_origVtable.SetClipper(This, lpDDClipper);
		if (SUCCEEDED(result) && CompatPrimarySurface::isPrimary(This))
		{
			RealPrimarySurface::setClipper(lpDDClipper);
		}
		return result;
	}

	template <typename TSurface>
	HRESULT SurfaceImpl<TSurface>::SetPalette(TSurface* This, LPDIRECTDRAWPALETTE lpDDPalette)
	{
		const bool isPrimary = CompatPrimarySurface::isPrimary(This);
		if (isPrimary)
		{
			if (lpDDPalette)
			{
				DirectDrawPalette::waitForNextUpdate();
			}
			if (lpDDPalette == CompatPrimarySurface::g_palette)
			{
				return DD_OK;
			}
		}

		HRESULT result = s_origVtable.SetPalette(This, lpDDPalette);
		if (isPrimary && SUCCEEDED(result))
		{
			CompatPrimarySurface::g_palette = lpDDPalette;
			RealPrimarySurface::setPalette();
		}
		return result;
	}

	template <typename TSurface>
	HRESULT SurfaceImpl<TSurface>::Unlock(TSurface* This, TUnlockParam lpRect)
	{
		HRESULT result = s_origVtable.Unlock(This, lpRect);
		if (SUCCEEDED(result) && CompatPrimarySurface::isPrimary(This))
		{
			RealPrimarySurface::update();
		}
		return result;
	}

	template <typename TSurface>
	void SurfaceImpl<TSurface>::restorePrimaryCaps(TDdsCaps& caps)
	{
		caps.dwCaps &= ~(DDSCAPS_OFFSCREENPLAIN | DDSCAPS_SYSTEMMEMORY);
		caps.dwCaps |= DDSCAPS_PRIMARYSURFACE | DDSCAPS_VISIBLE | DDSCAPS_VIDEOMEMORY | DDSCAPS_LOCALVIDMEM;
	}

	template <typename TSurface>
	const Vtable<TSurface>& SurfaceImpl<TSurface>::s_origVtable = CompatVtableBase<TSurface>::s_origVtable;

	template SurfaceImpl<IDirectDrawSurface>;
	template SurfaceImpl<IDirectDrawSurface2>;
	template SurfaceImpl<IDirectDrawSurface3>;
	template SurfaceImpl<IDirectDrawSurface4>;
	template SurfaceImpl<IDirectDrawSurface7>;

	template HRESULT SurfaceImpl<IDirectDrawSurface>::createCompatPrimarySurface(
		CompatRef<IDirectDraw> dd,
		TSurfaceDesc compatDesc,
		IDirectDrawSurface*& compatSurface);
	template HRESULT SurfaceImpl<IDirectDrawSurface>::createCompatPrimarySurface(
		CompatRef<IDirectDraw2> dd,
		TSurfaceDesc compatDesc,
		IDirectDrawSurface*& compatSurface);
	template HRESULT SurfaceImpl<IDirectDrawSurface4>::createCompatPrimarySurface(
		CompatRef<IDirectDraw4> dd,
		TSurfaceDesc compatDesc,
		IDirectDrawSurface4*& compatSurface);
	template HRESULT SurfaceImpl<IDirectDrawSurface7>::createCompatPrimarySurface(
		CompatRef<IDirectDraw7> dd,
		TSurfaceDesc compatDesc,
		IDirectDrawSurface7*& compatSurface);
}
