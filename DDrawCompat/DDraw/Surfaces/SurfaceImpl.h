#pragma once

#include <functional>

#include <ddraw.h>

#include "Common/CompatVtable.h"
#include "DDraw/Types.h"

namespace DDraw
{
	class Surface;

	template <typename TSurface>
	class SurfaceImpl2
	{
	public:
		SurfaceImpl2() : m_data(nullptr) {}

		virtual HRESULT GetDDInterface(TSurface* This, LPVOID* lplpDD);

	protected:
		friend class Surface;

		Surface* m_data;
	};

	template <typename TSurface>
	class SurfaceImpl : public SurfaceImpl2<TSurface>
	{
	public:
		typedef typename Types<TSurface>::TSurfaceDesc TSurfaceDesc;
		typedef typename Types<TSurface>::TDdsCaps TDdsCaps;
		typedef typename Types<TSurface>::TUnlockParam TUnlockParam;

		virtual ~SurfaceImpl();

		virtual HRESULT Blt(TSurface* This, LPRECT lpDestRect, TSurface* lpDDSrcSurface, LPRECT lpSrcRect,
			DWORD dwFlags, LPDDBLTFX lpDDBltFx);
		virtual HRESULT BltFast(TSurface* This, DWORD dwX, DWORD dwY,
			TSurface* lpDDSrcSurface, LPRECT lpSrcRect, DWORD dwTrans);
		virtual HRESULT Flip(TSurface* This, TSurface* lpDDSurfaceTargetOverride, DWORD dwFlags);
		virtual HRESULT GetBltStatus(TSurface* This, DWORD dwFlags);
		virtual HRESULT GetCaps(TSurface* This, TDdsCaps* lpDDSCaps);
		virtual HRESULT GetDC(TSurface* This, HDC* lphDC);
		virtual HRESULT GetFlipStatus(TSurface* This, DWORD dwFlags);
		virtual HRESULT GetSurfaceDesc(TSurface* This, TSurfaceDesc* lpDDSurfaceDesc);
		virtual HRESULT IsLost(TSurface* This);
		virtual HRESULT Lock(TSurface* This, LPRECT lpDestRect, TSurfaceDesc* lpDDSurfaceDesc,
			DWORD dwFlags, HANDLE hEvent);
		virtual HRESULT ReleaseDC(TSurface* This, HDC hDC);
		virtual HRESULT Restore(TSurface* This);
		virtual HRESULT SetPalette(TSurface* This, LPDIRECTDRAWPALETTE lpDDPalette);
		virtual HRESULT Unlock(TSurface* This, TUnlockParam lpRect);

	protected:
		static const Vtable<TSurface>& s_origVtable;

	private:
		bool waitForFlip(TSurface* This, DWORD flags, DWORD waitFlag, DWORD doNotWaitFlag);
	};
}
