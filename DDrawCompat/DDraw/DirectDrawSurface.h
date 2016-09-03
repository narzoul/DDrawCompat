#pragma once

#include "Common/CompatRef.h"
#include "Common/CompatVtable.h"
#include "DDraw/Visitors/DirectDrawSurfaceVtblVisitor.h"
#include "DDraw/Types.h"

namespace DDraw
{
	template <typename TSurface>
	class DirectDrawSurface : public CompatVtable<DirectDrawSurface<TSurface>, TSurface>
	{
	public:
		typedef typename Types<TSurface>::TSurfaceDesc TSurfaceDesc;
		typedef typename Types<TSurface>::TDdsCaps TDdsCaps;
		typedef typename Types<TSurface>::TUnlockParam TUnlockParam;

		static void setCompatVtable(Vtable<TSurface>& vtable);

		template <typename TDirectDraw>
		static HRESULT createCompatPrimarySurface(
			CompatRef<TDirectDraw> dd,
			TSurfaceDesc compatDesc,
			TSurface*& compatSurface);

		static void fixSurfacePtrs(CompatRef<TSurface> surface);

		static HRESULT STDMETHODCALLTYPE Blt(
			TSurface* This,
			LPRECT lpDestRect,
			TSurface* lpDDSrcSurface,
			LPRECT lpSrcRect,
			DWORD dwFlags,
			LPDDBLTFX lpDDBltFx);

		static HRESULT STDMETHODCALLTYPE BltFast(
			TSurface* This,
			DWORD dwX,
			DWORD dwY,
			TSurface* lpDDSrcSurface,
			LPRECT lpSrcRect,
			DWORD dwTrans);

		static HRESULT STDMETHODCALLTYPE Flip(
			TSurface* This,
			TSurface* lpDDSurfaceTargetOverride,
			DWORD dwFlags);

		static HRESULT STDMETHODCALLTYPE GetCaps(TSurface* This, TDdsCaps* lpDDSCaps);
		static HRESULT STDMETHODCALLTYPE GetSurfaceDesc(TSurface* This, TSurfaceDesc* lpDDSurfaceDesc);
		static HRESULT STDMETHODCALLTYPE IsLost(TSurface* This);

		static HRESULT STDMETHODCALLTYPE Lock(
			TSurface* This,
			LPRECT lpDestRect,
			TSurfaceDesc* lpDDSurfaceDesc,
			DWORD dwFlags,
			HANDLE hEvent);

		static HRESULT STDMETHODCALLTYPE QueryInterface(TSurface* This, REFIID riid, LPVOID* obp);
		static HRESULT STDMETHODCALLTYPE ReleaseDC(TSurface* This, HDC hDC);
		static HRESULT STDMETHODCALLTYPE Restore(TSurface* This);
		static HRESULT STDMETHODCALLTYPE SetClipper(TSurface* This, LPDIRECTDRAWCLIPPER lpDDClipper);
		static HRESULT STDMETHODCALLTYPE SetPalette(TSurface* This, LPDIRECTDRAWPALETTE lpDDPalette);
		static HRESULT STDMETHODCALLTYPE Unlock(TSurface* This, TUnlockParam lpRect);

	private:
		static void restorePrimaryCaps(TDdsCaps& caps);
	};
}
