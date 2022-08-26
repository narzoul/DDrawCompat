#pragma once

#include <ddraw.h>

#include <Common/CompatPtr.h>
#include <Common/CompatRef.h>
#include <Common/CompatVtable.h>
#include <DDraw/Surfaces/SurfaceImpl.h>
#include <DDraw/Types.h>

namespace DDraw
{
	class PalettizedTexture;

	template <typename TSurface>
	class PalettizedTextureImpl : public SurfaceImpl<TSurface>
	{
	public:
		PalettizedTextureImpl(PalettizedTexture& data, CompatPtr<TSurface> palettizedSurface);

		virtual HRESULT Blt(TSurface* This, LPRECT lpDestRect, TSurface* lpDDSrcSurface, LPRECT lpSrcRect,
			DWORD dwFlags, LPDDBLTFX lpDDBltFx) override;
		virtual HRESULT BltFast(TSurface* This, DWORD dwX, DWORD dwY,
			TSurface* lpDDSrcSurface, LPRECT lpSrcRect, DWORD dwTrans) override;
		virtual HRESULT GetCaps(TSurface* This, TDdsCaps* lpDDSCaps) override;
		virtual HRESULT GetDC(TSurface* This, HDC* lphDC);
		virtual HRESULT GetPalette(TSurface* This, LPDIRECTDRAWPALETTE* lplpDDPalette) override;
		virtual HRESULT GetSurfaceDesc(TSurface* This, TSurfaceDesc* lpDDSurfaceDesc) override;
		virtual HRESULT Lock(TSurface* This, LPRECT lpDestRect, TSurfaceDesc* lpDDSurfaceDesc,
			DWORD dwFlags, HANDLE hEvent) override;
		virtual HRESULT ReleaseDC(TSurface* This, HDC hDC) override;
		virtual HRESULT Restore(TSurface* This) override;
		virtual HRESULT SetPalette(TSurface* This, LPDIRECTDRAWPALETTE lpDDPalette) override;
		virtual HRESULT Unlock(TSurface* This, TUnlockParam lpRect) override;

		virtual TSurface* getBltSrc(TSurface* src) override;

	private:
		PalettizedTexture& m_data;
		CompatPtr<TSurface> m_palettizedSurface;
	};
}
