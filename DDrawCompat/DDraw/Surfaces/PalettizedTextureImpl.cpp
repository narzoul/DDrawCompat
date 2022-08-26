#include <Common/CompatPtr.h>
#include <DDraw/Surfaces/PalettizedTexture.h>
#include <DDraw/Surfaces/PalettizedTextureImpl.h>

namespace DDraw
{
	template <typename TSurface>
	PalettizedTextureImpl<TSurface>::PalettizedTextureImpl(PalettizedTexture& data, CompatPtr<TSurface> palettizedSurface)
		: SurfaceImpl(&data)
		, m_data(data)
		, m_palettizedSurface(palettizedSurface)
	{
	}

	template <typename TSurface>
	HRESULT PalettizedTextureImpl<TSurface>::Blt(
		TSurface* /*This*/, LPRECT lpDestRect, TSurface* lpDDSrcSurface, LPRECT lpSrcRect,
		DWORD dwFlags, LPDDBLTFX lpDDBltFx)
	{
		return SurfaceImpl::Blt(m_palettizedSurface, lpDestRect, lpDDSrcSurface, lpSrcRect, dwFlags, lpDDBltFx);
	}

	template <typename TSurface>
	HRESULT PalettizedTextureImpl<TSurface>::BltFast(
		TSurface* /*This*/, DWORD dwX, DWORD dwY, TSurface* lpDDSrcSurface, LPRECT lpSrcRect, DWORD dwTrans)
	{
		return SurfaceImpl::BltFast(m_palettizedSurface, dwX, dwY, lpDDSrcSurface, lpSrcRect, dwTrans);
	}

	template <typename TSurface>
	TSurface* PalettizedTextureImpl<TSurface>::getBltSrc(TSurface* /*src*/)
	{
		return m_palettizedSurface;
	}

	template <typename TSurface>
	HRESULT PalettizedTextureImpl<TSurface>::GetCaps(TSurface* /*This*/, TDdsCaps* lpDDSCaps)
	{
		return SurfaceImpl::GetCaps(m_palettizedSurface, lpDDSCaps);
	}

	template <typename TSurface>
	HRESULT PalettizedTextureImpl<TSurface>::GetDC(TSurface* /*This*/, HDC* lphDC)
	{
		return SurfaceImpl::GetDC(m_palettizedSurface, lphDC);
	}

	template <typename TSurface>
	HRESULT PalettizedTextureImpl<TSurface>::GetPalette(TSurface* /*This*/, LPDIRECTDRAWPALETTE* lplpDDPalette)
	{
		return SurfaceImpl::GetPalette(m_palettizedSurface, lplpDDPalette);
	}

	template <typename TSurface>
	HRESULT PalettizedTextureImpl<TSurface>::GetSurfaceDesc(TSurface* /*This*/, TSurfaceDesc* lpDDSurfaceDesc)
	{
		return SurfaceImpl::GetSurfaceDesc(m_palettizedSurface, lpDDSurfaceDesc);
	}

	template <typename TSurface>
	HRESULT PalettizedTextureImpl<TSurface>::Lock(
		TSurface* /*This*/, LPRECT lpDestRect, TSurfaceDesc* lpDDSurfaceDesc, DWORD dwFlags, HANDLE hEvent)
	{
		return SurfaceImpl::Lock(m_palettizedSurface, lpDestRect, lpDDSurfaceDesc, dwFlags, hEvent);
	}

	template <typename TSurface>
	HRESULT PalettizedTextureImpl<TSurface>::ReleaseDC(TSurface* /*This*/, HDC hDC)
	{
		return SurfaceImpl::ReleaseDC(m_palettizedSurface, hDC);
	}

	template <typename TSurface>
	HRESULT PalettizedTextureImpl<TSurface>::Restore(TSurface* This)
	{
		HRESULT result = SurfaceImpl::Restore(m_palettizedSurface);
		if (SUCCEEDED(result))
		{
			result = SurfaceImpl::Restore(This);
		}
		return result;
	}

	template <typename TSurface>
	HRESULT PalettizedTextureImpl<TSurface>::SetPalette(TSurface* /*This*/, LPDIRECTDRAWPALETTE lpDDPalette)
	{
		return SurfaceImpl::SetPalette(m_palettizedSurface, lpDDPalette);
	}

	template <typename TSurface>
	HRESULT PalettizedTextureImpl<TSurface>::Unlock(TSurface* /*This*/, TUnlockParam lpRect)
	{
		return SurfaceImpl::Unlock(m_palettizedSurface, lpRect);
	}

	template PalettizedTextureImpl<IDirectDrawSurface>;
	template PalettizedTextureImpl<IDirectDrawSurface2>;
	template PalettizedTextureImpl<IDirectDrawSurface3>;
	template PalettizedTextureImpl<IDirectDrawSurface4>;
	template PalettizedTextureImpl<IDirectDrawSurface7>;
}
