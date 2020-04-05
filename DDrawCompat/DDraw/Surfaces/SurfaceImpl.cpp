#include <set>

#include "DDraw/DirectDrawSurface.h"
#include "DDraw/RealPrimarySurface.h"
#include "DDraw/Surfaces/PrimarySurface.h"
#include "DDraw/Surfaces/Surface.h"
#include "DDraw/Surfaces/SurfaceImpl.h"

namespace DDraw
{
	template <typename TSurface>
	SurfaceImpl<TSurface>::SurfaceImpl(Surface* data)
		: m_data(data)
	{
	}

	template <typename TSurface>
	SurfaceImpl<TSurface>::~SurfaceImpl()
	{
	}

	template <typename TSurface>
	HRESULT SurfaceImpl<TSurface>::Blt(
		TSurface* This, LPRECT lpDestRect, TSurface* lpDDSrcSurface, LPRECT lpSrcRect,
		DWORD dwFlags, LPDDBLTFX lpDDBltFx)
	{
		if (!waitForFlip(This, dwFlags, DDBLT_WAIT, DDBLT_DONOTWAIT))
		{
			return DDERR_WASSTILLDRAWING;
		}
		return s_origVtable.Blt(This, lpDestRect, lpDDSrcSurface, lpSrcRect, dwFlags, lpDDBltFx);
	}

	template <typename TSurface>
	HRESULT SurfaceImpl<TSurface>::BltFast(
		TSurface* This, DWORD dwX, DWORD dwY, TSurface* lpDDSrcSurface, LPRECT lpSrcRect, DWORD dwTrans)
	{
		return s_origVtable.BltFast(This, dwX, dwY, lpDDSrcSurface, lpSrcRect, dwTrans);
	}

	template <typename TSurface>
	HRESULT SurfaceImpl<TSurface>::Flip(TSurface* This, TSurface* lpDDSurfaceTargetOverride, DWORD dwFlags)
	{
		return s_origVtable.Flip(This, lpDDSurfaceTargetOverride, dwFlags);
	}

	template <typename TSurface>
	HRESULT SurfaceImpl<TSurface>::GetBltStatus(TSurface* This, DWORD dwFlags)
	{
		HRESULT result = s_origVtable.GetBltStatus(This, dwFlags);
		if (SUCCEEDED(result) && (dwFlags & DDGBS_CANBLT))
		{
			const bool wait = false;
			if (!RealPrimarySurface::waitForFlip(m_data, wait))
			{
				return DDERR_WASSTILLDRAWING;
			}
		}
		return result;
	}

	template <typename TSurface>
	HRESULT SurfaceImpl<TSurface>::GetCaps(TSurface* This, TDdsCaps* lpDDSCaps)
	{
		return s_origVtable.GetCaps(This, lpDDSCaps);
	}

	template <typename TSurface>
	HRESULT SurfaceImpl<TSurface>::GetDC(TSurface* This, HDC* lphDC)
	{
		HRESULT result = s_origVtable.GetDC(This, lphDC);
		if (SUCCEEDED(result))
		{
			RealPrimarySurface::waitForFlip(m_data);
		}
		return result;
	}

	template <typename TSurface>
	HRESULT SurfaceImpl<TSurface>::GetFlipStatus(TSurface* This, DWORD dwFlags)
	{
		HRESULT result = s_origVtable.GetFlipStatus(This, dwFlags);
		if (SUCCEEDED(result))
		{
			const bool wait = false;
			if (!RealPrimarySurface::waitForFlip(m_data, wait))
			{
				return DDERR_WASSTILLDRAWING;
			}
		}
		return result;
	}

	template <typename TSurface>
	HRESULT SurfaceImpl<TSurface>::GetSurfaceDesc(TSurface* This, TSurfaceDesc* lpDDSurfaceDesc)
	{
		return s_origVtable.GetSurfaceDesc(This, lpDDSurfaceDesc);
	}

	template <typename TSurface>
	HRESULT SurfaceImpl<TSurface>::IsLost(TSurface* This)
	{
		return s_origVtable.IsLost(This);
	}

	template <typename TSurface>
	HRESULT SurfaceImpl<TSurface>::Lock(
		TSurface* This, LPRECT lpDestRect, TSurfaceDesc* lpDDSurfaceDesc,
		DWORD dwFlags, HANDLE hEvent)
	{
		if (!waitForFlip(This, dwFlags, DDLOCK_WAIT, DDLOCK_DONOTWAIT))
		{
			return DDERR_WASSTILLDRAWING;
		}

		HRESULT result = s_origVtable.Lock(This, lpDestRect, lpDDSurfaceDesc, dwFlags, hEvent);
		if (DDERR_SURFACELOST == result)
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

		return result;
	}

	template <typename TSurface>
	HRESULT SurfaceImpl<TSurface>::QueryInterface(TSurface* This, REFIID riid, LPVOID* obp)
	{
		return s_origVtable.QueryInterface(This, (IID_IDirect3DRampDevice == riid ? IID_IDirect3DRGBDevice : riid), obp);
	}

	template <typename TSurface>
	HRESULT SurfaceImpl<TSurface>::ReleaseDC(TSurface* This, HDC hDC)
	{
		return s_origVtable.ReleaseDC(This, hDC);
	}

	template <typename TSurface>
	HRESULT SurfaceImpl<TSurface>::Restore(TSurface* This)
	{
		HRESULT result = s_origVtable.Restore(This);
		if (SUCCEEDED(result))
		{
			m_data->restore();
		}
		return result;
	}

	template <typename TSurface>
	HRESULT SurfaceImpl<TSurface>::SetPalette(TSurface* This, LPDIRECTDRAWPALETTE lpDDPalette)
	{
		return s_origVtable.SetPalette(This, lpDDPalette);
	}

	template <typename TSurface>
	HRESULT SurfaceImpl<TSurface>::Unlock(TSurface* This, TUnlockParam lpRect)
	{
		return s_origVtable.Unlock(This, lpRect);
	}

	template <typename TSurface>
	bool SurfaceImpl<TSurface>::waitForFlip(TSurface* This, DWORD flags, DWORD waitFlag, DWORD doNotWaitFlag)
	{
		const bool wait = (flags & waitFlag) || !(flags & doNotWaitFlag) &&
			CompatVtable<IDirectDrawSurface7Vtbl>::s_origVtablePtr == static_cast<void*>(This->lpVtbl);
		return DDraw::RealPrimarySurface::waitForFlip(m_data, wait);
	}

	template <typename TSurface>
	const Vtable<TSurface>& SurfaceImpl<TSurface>::s_origVtable =
		CompatVtable<Vtable<TSurface>>::s_origVtable;

	template SurfaceImpl<IDirectDrawSurface>;
	template SurfaceImpl<IDirectDrawSurface2>;
	template SurfaceImpl<IDirectDrawSurface3>;
	template SurfaceImpl<IDirectDrawSurface4>;
	template SurfaceImpl<IDirectDrawSurface7>;
}
