#pragma once

#include "Common/CompatVtable.h"
#include "DDraw/Visitors/DirectDrawVtblVisitor.h"
#include "DDraw/Types.h"

namespace DDraw
{
	template <typename TDirectDraw>
	class DirectDraw: public CompatVtable<DirectDraw<TDirectDraw>, TDirectDraw>
	{
	public:
		typedef typename Types<TDirectDraw>::TCreatedSurface TSurface;
		typedef typename Types<TDirectDraw>::TSurfaceDesc TSurfaceDesc;

		static void setCompatVtable(Vtable<TDirectDraw>& vtable);

		static HRESULT STDMETHODCALLTYPE CreateSurface(
			TDirectDraw* This,
			TSurfaceDesc* lpDDSurfaceDesc,
			TSurface** lplpDDSurface,
			IUnknown* pUnkOuter);

		static HRESULT STDMETHODCALLTYPE GetDisplayMode(TDirectDraw* This, TSurfaceDesc* lpDDSurfaceDesc);
		static HRESULT STDMETHODCALLTYPE RestoreDisplayMode(TDirectDraw* This);
		static HRESULT STDMETHODCALLTYPE SetCooperativeLevel(TDirectDraw* This, HWND hWnd, DWORD dwFlags);

		template <typename... Params>
		static HRESULT STDMETHODCALLTYPE SetDisplayMode(
			TDirectDraw* This,
			DWORD dwWidth,
			DWORD dwHeight,
			DWORD dwBPP,
			Params... params);
	};
}
