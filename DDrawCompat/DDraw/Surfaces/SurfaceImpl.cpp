#include <set>

#include "DDraw/Surfaces/Surface.h"
#include "DDraw/Surfaces/SurfaceImpl.h"

namespace
{
	struct DirectDrawInterface
	{
		const void* vtable;
		void* ddObject;
		DirectDrawInterface* next;
		DWORD refCount;
		DWORD unknown1;
		DWORD unknown2;
	};

	void fixSurfacePtr(CompatRef<IDirectDrawSurface7> surface, const DDSURFACEDESC2& desc)
	{
		if ((desc.ddsCaps.dwCaps & DDSCAPS_SYSTEMMEMORY) || 0 == desc.dwWidth || 0 == desc.dwHeight)
		{
			return;
		}

		RECT r = { 0, 0, 1, 1 };
		surface->Blt(&surface, &r, &surface, &r, DDBLT_WAIT, nullptr);
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
}

namespace DDraw
{
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
	void SurfaceImpl<TSurface>::undoFlip(TSurface* This, TSurface* targetOverride)
	{
		if (targetOverride)
		{
			SurfaceImpl::Flip(This, targetOverride, DDFLIP_WAIT);
		}
		else
		{
			TSurfaceDesc desc = {};
			desc.dwSize = sizeof(desc);
			s_origVtable.GetSurfaceDesc(This, &desc);

			for (DWORD i = 0; i < desc.dwBackBufferCount; ++i)
			{
				SurfaceImpl::Flip(This, nullptr, DDFLIP_WAIT);
			}
		}
	}

	template <typename TSurface>
	HRESULT SurfaceImpl<TSurface>::Blt(
		TSurface* This, LPRECT lpDestRect, TSurface* lpDDSrcSurface, LPRECT lpSrcRect,
		DWORD dwFlags, LPDDBLTFX lpDDBltFx)
	{
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
	HRESULT SurfaceImpl<TSurface>::GetCaps(TSurface* This, TDdsCaps* lpDDSCaps)
	{
		return s_origVtable.GetCaps(This, lpDDSCaps);
	}
	
	template <typename TSurface>
	HRESULT SurfaceImpl2<TSurface>::GetDDInterface(TSurface* /*This*/, LPVOID* lplpDD)
	{
		DirectDrawInterface dd = {};
		dd.vtable = IID_IDirectDraw7 == m_data->m_ddId
			? static_cast<const void*>(CompatVtable<IDirectDrawVtbl>::s_origVtablePtr)
			: static_cast<const void*>(CompatVtable<IDirectDraw7Vtbl>::s_origVtablePtr);
		dd.ddObject = m_data->m_ddObject;
		return CompatVtable<IDirectDrawVtbl>::s_origVtable.QueryInterface(
			reinterpret_cast<IDirectDraw*>(&dd), m_data->m_ddId, lplpDD);
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
		return s_origVtable.QueryInterface(This, riid, obp);
	}

	template <typename TSurface>
	HRESULT SurfaceImpl<TSurface>::ReleaseDC(TSurface* This, HDC hDC)
	{
		return s_origVtable.ReleaseDC(This, hDC);
	}

	template <typename TSurface>
	HRESULT SurfaceImpl<TSurface>::Restore(TSurface* This)
	{
		return s_origVtable.Restore(This);
	}

	template <typename TSurface>
	HRESULT SurfaceImpl<TSurface>::SetClipper(TSurface* This, LPDIRECTDRAWCLIPPER lpDDClipper)
	{
		return s_origVtable.SetClipper(This, lpDDClipper);
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
	const Vtable<TSurface>& SurfaceImpl<TSurface>::s_origVtable =
		CompatVtable<Vtable<TSurface>>::s_origVtable;

	template SurfaceImpl<IDirectDrawSurface>;
	template SurfaceImpl<IDirectDrawSurface2>;
	template SurfaceImpl<IDirectDrawSurface3>;
	template SurfaceImpl<IDirectDrawSurface4>;
	template SurfaceImpl<IDirectDrawSurface7>;
}
