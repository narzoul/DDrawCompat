#include <set>
#include <type_traits>

#include <d3d.h>
#include <d3dumddi.h>

#include <Common/CompatPtr.h>
#include <D3dDdi/Device.h>
#include <D3dDdi/Resource.h>
#include <DDraw/DirectDrawSurface.h>
#include <DDraw/LogUsedResourceFormat.h>
#include <DDraw/RealPrimarySurface.h>
#include <DDraw/Surfaces/PrimarySurface.h>
#include <DDraw/Surfaces/Surface.h>
#include <DDraw/Surfaces/SurfaceImpl.h>
#include <Direct3d/Direct3d.h>
#include <Dll/Dll.h>
#include <Gdi/Region.h>
#include <Gdi/WinProc.h>

namespace
{
	HANDLE g_bltSrcResource = nullptr;
	UINT g_bltSrcSubResourceIndex = 0;
	RECT g_bltSrcRect = {};

	IDirectDrawClipper* createClipper()
	{
		IDirectDrawClipper* clipper = nullptr;
		CALL_ORIG_PROC(DirectDrawCreateClipper)(0, &clipper, nullptr);
		return clipper;
	}

	IDirectDrawClipper* getFixedClipper(CompatWeakPtr<IDirectDrawClipper> clipper)
	{
		HWND hwnd = nullptr;
		clipper->GetHWnd(clipper, &hwnd);
		if (!hwnd)
		{
			return nullptr;
		}

		static CompatWeakPtr<IDirectDrawClipper> fixedClipper(createClipper());
		if (!fixedClipper)
		{
			return nullptr;
		}

		HDC dc = GetDCEx(hwnd, nullptr, DCX_CACHE | DCX_USESTYLE);
		Gdi::Region rgn;
		GetRandomRgn(dc, rgn, SYSRGN);
		CALL_ORIG_FUNC(ReleaseDC)(hwnd, dc);

		auto& mi = DDraw::PrimarySurface::getMonitorInfo();
		if (0 != mi.rcEmulated.left || 0 != mi.rcEmulated.top)
		{
			rgn.offset(-mi.rcEmulated.left, -mi.rcEmulated.top);
		}

		DWORD rgnSize = GetRegionData(rgn, 0, nullptr);
		std::vector<unsigned char> rgnDataBuf(std::max<DWORD>(rgnSize, sizeof(RGNDATA) + sizeof(RECT)));
		RGNDATA* rgnData = reinterpret_cast<RGNDATA*>(rgnDataBuf.data());
		GetRegionData(rgn, rgnSize, rgnData);
		if (0 == rgnData->rdh.nCount)
		{
			rgnData->rdh.nCount = 1;
		}

		fixedClipper->SetClipList(fixedClipper, rgnData, 0);
		return fixedClipper;
	}

	template <typename TSurface>
	class ClipperFix
	{
	public:
		ClipperFix(TSurface* surface) : m_surface(surface)
		{
			HRESULT result = getOrigVtable(surface).GetClipper(surface, &m_clipper.getRef());
			if (FAILED(result))
			{
				return;
			}

			auto fixedClipper = getFixedClipper(m_clipper);
			if (!fixedClipper)
			{
				m_clipper.release();
				return;
			}

			getOrigVtable(surface).SetClipper(surface, fixedClipper);
		}

		~ClipperFix()
		{
			if (m_clipper)
			{
				getOrigVtable(m_surface).SetClipper(m_surface, m_clipper);
			}
		}

	private:
		TSurface* m_surface;
		CompatPtr<IDirectDrawClipper> m_clipper;
	};

	template <typename TSurface>
	typename DDraw::Types<TSurface>::TDdsCaps getCaps(TSurface* This)
	{
		DDraw::Types<TSurface>::TDdsCaps caps = {};
		getOrigVtable(This).GetCaps(This, &caps);
		return caps;
	}

	template <typename TSurface>
	typename DDraw::Types<TSurface>::TSurfaceDesc getDesc(TSurface* This)
	{
		DDraw::Types<TSurface>::TSurfaceDesc desc = {};
		desc.dwSize = sizeof(desc);
		getOrigVtable(This).GetSurfaceDesc(This, &desc);
		return desc;
	}

	template <typename TSurfaceDesc>
	RECT getRect(LPRECT rect, const TSurfaceDesc& desc)
	{
		return rect ? *rect : RECT{ 0, 0, static_cast<LONG>(desc.dwWidth), static_cast<LONG>(desc.dwHeight) };
	}

	template <typename TSurface, typename BltFunc>
	HRESULT blt(TSurface* This, TSurface* lpDDSrcSurface, LPRECT lpSrcRect, BltFunc bltFunc)
	{
		if (!lpDDSrcSurface)
		{
			return bltFunc(This, lpDDSrcSurface, lpSrcRect);
		}

		auto srcSurface = DDraw::Surface::getSurface(*lpDDSrcSurface);
		if (srcSurface)
		{
			lpDDSrcSurface = srcSurface->getImpl<TSurface>()->getBltSrc(lpDDSrcSurface);
		}

		auto dstCaps = getCaps(This);
		if (!(dstCaps.dwCaps & DDSCAPS_3DDEVICE) || !(dstCaps.dwCaps & DDSCAPS_VIDEOMEMORY))
		{
			return bltFunc(This, lpDDSrcSurface, lpSrcRect);
		}

		auto srcDesc = getDesc(lpDDSrcSurface);
		if (!(srcDesc.ddsCaps.dwCaps & DDSCAPS_SYSTEMMEMORY))
		{
			return bltFunc(This, lpDDSrcSurface, lpSrcRect);
		}

		auto srcResource = D3dDdi::Device::findResource(DDraw::DirectDrawSurface::getDriverResourceHandle(*lpDDSrcSurface));
		if (!srcResource)
		{
			return bltFunc(This, lpDDSrcSurface, lpSrcRect);
		}

		auto& repo = srcResource->getDevice().getRepo();
		RECT srcRect = getRect(lpSrcRect, srcDesc);
		auto& tex = repo.getTempTexture(srcRect.right - srcRect.left, srcRect.bottom - srcRect.top,
			srcResource->getOrigDesc().Format);
		if (!tex.resource)
		{
			return bltFunc(This, lpDDSrcSurface, lpSrcRect);
		}

		CompatPtr<TSurface> newSrcSurface(tex.surface);
		DDCOLORKEY ck = {};
		HRESULT result = getOrigVtable(This).GetColorKey(lpDDSrcSurface, DDCKEY_SRCBLT, &ck);
		getOrigVtable(This).SetColorKey(newSrcSurface, DDCKEY_SRCBLT, SUCCEEDED(result) ? &ck : nullptr);

		g_bltSrcResource = *srcResource;
		g_bltSrcSubResourceIndex = DDraw::DirectDrawSurface::getSubResourceIndex(*lpDDSrcSurface);
		g_bltSrcRect = getRect(lpSrcRect, srcDesc);

		RECT r = { 0, 0, g_bltSrcRect.right - g_bltSrcRect.left, g_bltSrcRect.bottom - g_bltSrcRect.top };
		result = bltFunc(This, newSrcSurface, &r);

		g_bltSrcResource = nullptr;
		g_bltSrcSubResourceIndex = 0;
		g_bltSrcRect = {};

		return SUCCEEDED(result) ? DD_OK : bltFunc(This, lpDDSrcSurface, lpSrcRect);
	}
}

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
	HRESULT SurfaceImpl<TSurface>::AddAttachedSurface(TSurface* This, TSurface* lpDDSAttachedSurface)
	{
		return getOrigVtable(This).AddAttachedSurface(This, lpDDSAttachedSurface);
	}

	template <typename TSurface>
	HRESULT SurfaceImpl<TSurface>::Blt(
		TSurface* This, LPRECT lpDestRect, TSurface* lpDDSrcSurface, LPRECT lpSrcRect,
		DWORD dwFlags, LPDDBLTFX lpDDBltFx)
	{
		Gdi::WinProc::startFrame();
		RealPrimarySurface::waitForFlip(m_data->getDDS());
		ClipperFix fix(This);
		return blt(This, lpDDSrcSurface, lpSrcRect, [=](TSurface* This, TSurface* lpDDSrcSurface, LPRECT lpSrcRect)
			{ return getOrigVtable(This).Blt(This, lpDestRect, lpDDSrcSurface, lpSrcRect, dwFlags, lpDDBltFx); });
	}

	template <typename TSurface>
	HRESULT SurfaceImpl<TSurface>::BltFast(
		TSurface* This, DWORD dwX, DWORD dwY, TSurface* lpDDSrcSurface, LPRECT lpSrcRect, DWORD dwTrans)
	{
		Gdi::WinProc::startFrame();
		RealPrimarySurface::waitForFlip(m_data->getDDS());
		return blt(This, lpDDSrcSurface, lpSrcRect, [=](TSurface* This, TSurface* lpDDSrcSurface, LPRECT lpSrcRect)
			{ return getOrigVtable(This).BltFast(This, dwX, dwY, lpDDSrcSurface, lpSrcRect, dwTrans); });
	}

	template <typename TSurface>
	HRESULT SurfaceImpl<TSurface>::Flip(TSurface* This, TSurface* lpDDSurfaceTargetOverride, DWORD dwFlags)
	{
		return getOrigVtable(This).Flip(This, lpDDSurfaceTargetOverride, dwFlags);
	}

	template <typename TSurface>
	HRESULT SurfaceImpl<TSurface>::GetAttachedSurface(TSurface* This, TDdsCaps* lpDDSCaps, TSurface** lplpDDAttachedSurface)
	{
		TDdsCaps caps = {};
		if (lpDDSCaps && (lpDDSCaps->dwCaps & DDSCAPS_3DDEVICE) &&
			SUCCEEDED(getOrigVtable(This).GetCaps(This, &caps)) &&
			!(caps.dwCaps & DDSCAPS_3DDEVICE) && (m_data->m_origCaps & DDSCAPS_3DDEVICE))
		{
			caps = *lpDDSCaps;
			caps.dwCaps &= ~DDSCAPS_3DDEVICE;
			return getOrigVtable(This).GetAttachedSurface(This, &caps, lplpDDAttachedSurface);
		}
		return getOrigVtable(This).GetAttachedSurface(This, lpDDSCaps, lplpDDAttachedSurface);
	}

	template <typename TSurface>
	TSurface* SurfaceImpl<TSurface>::getBltSrc(TSurface* src)
	{
		return src;
	}

	template <typename TSurface>
	HRESULT SurfaceImpl<TSurface>::GetCaps(TSurface* This, TDdsCaps* lpDDSCaps)
	{
		HRESULT result = getOrigVtable(This).GetCaps(This, lpDDSCaps);
		if (SUCCEEDED(result))
		{
			restoreOrigCaps(lpDDSCaps->dwCaps);
		}
		return result;
	}

	template <typename TSurface>
	HRESULT SurfaceImpl<TSurface>::GetDC(TSurface* This, HDC* lphDC)
	{
		Gdi::WinProc::startFrame();
		RealPrimarySurface::waitForFlip(m_data->getDDS());
		HRESULT result = getOrigVtable(This).GetDC(This, lphDC);
		if (SUCCEEDED(result))
		{
			Dll::g_origProcs.ReleaseDDThreadLock();
		}
		return result;
	}

	template <typename TSurface>
	HRESULT SurfaceImpl<TSurface>::GetPalette(TSurface* This, LPDIRECTDRAWPALETTE* lplpDDPalette)
	{
		return getOrigVtable(This).GetPalette(This, lplpDDPalette);
	}

	template <typename TSurface>
	HRESULT SurfaceImpl<TSurface>::GetSurfaceDesc(TSurface* This, TSurfaceDesc* lpDDSurfaceDesc)
	{
		HRESULT result = getOrigVtable(This).GetSurfaceDesc(This, lpDDSurfaceDesc);
		if (SUCCEEDED(result))
		{
			if (0 != m_data->m_sizeOverride.cx)
			{
				lpDDSurfaceDesc->dwWidth = m_data->m_sizeOverride.cx;
				lpDDSurfaceDesc->dwHeight = m_data->m_sizeOverride.cy;
				m_data->m_sizeOverride = {};
			}

			restoreOrigCaps(lpDDSurfaceDesc->ddsCaps.dwCaps);

			if ((m_data->m_origFlags & DDSD_MIPMAPCOUNT) && !(lpDDSurfaceDesc->dwFlags & DDSD_MIPMAPCOUNT))
			{
				lpDDSurfaceDesc->dwFlags |= DDSD_MIPMAPCOUNT;
				lpDDSurfaceDesc->dwMipMapCount = 1;
			}
		}
		return result;
	}

	template <typename TSurface>
	HRESULT SurfaceImpl<TSurface>::IsLost(TSurface* This)
	{
		return getOrigVtable(This).IsLost(This);
	}

	template <typename TSurface>
	HRESULT SurfaceImpl<TSurface>::Lock(
		TSurface* This, LPRECT lpDestRect, TSurfaceDesc* lpDDSurfaceDesc,
		DWORD dwFlags, HANDLE hEvent)
	{
		Gdi::WinProc::startFrame();
		RealPrimarySurface::waitForFlip(m_data->getDDS());
		HRESULT result = getOrigVtable(This).Lock(This, lpDestRect, lpDDSurfaceDesc, dwFlags, hEvent);
		if (SUCCEEDED(result))
		{
			restoreOrigCaps(lpDDSurfaceDesc->ddsCaps.dwCaps);
		}
		return result;
	}

	template <typename TSurface>
	HRESULT SurfaceImpl<TSurface>::QueryInterface(TSurface* This, REFIID riid, LPVOID* obp)
	{
		DDraw::SuppressResourceFormatLogs suppressResourceFormatLogs;
		auto& iid = Direct3d::replaceDevice(riid);
		HRESULT result = getOrigVtable(This).QueryInterface(This, iid, obp);
		if (DDERR_INVALIDOBJECT == result)
		{
			m_data->setSizeOverride(1, 1);
			result = getOrigVtable(This).QueryInterface(This, iid, obp);
			m_data->setSizeOverride(0, 0);
		}
		return result;
	}

	template <typename TSurface>
	HRESULT SurfaceImpl<TSurface>::ReleaseDC(TSurface* This, HDC hDC)
	{
		HRESULT result = getOrigVtable(This).ReleaseDC(This, hDC);
		if (SUCCEEDED(result))
		{
			Dll::g_origProcs.AcquireDDThreadLock();
		}
		return result;
	}

	template <typename TSurface>
	HRESULT SurfaceImpl<TSurface>::Restore(TSurface* This)
	{
		HRESULT result = getOrigVtable(This).Restore(This);
		if (SUCCEEDED(result))
		{
			m_data->restore();
		}
		return result;
	}

	template <typename TSurface>
	HRESULT SurfaceImpl<TSurface>::SetPalette(TSurface* This, LPDIRECTDRAWPALETTE lpDDPalette)
	{
		return getOrigVtable(This).SetPalette(This, lpDDPalette);
	}

	template <typename TSurface>
	HRESULT SurfaceImpl<TSurface>::SetSurfaceDesc(TSurface* This, TSurfaceDesc* lpddsd, DWORD dwFlags)
	{
		if constexpr (!std::is_same_v<TSurface, IDirectDrawSurface> && !std::is_same_v<TSurface, IDirectDrawSurface2>)
		{
			HRESULT result = getOrigVtable(This).SetSurfaceDesc(This, lpddsd, dwFlags);
			if (SUCCEEDED(result) && (lpddsd->dwFlags & DDSD_LPSURFACE))
			{
				m_data->m_sysMemBuffer.reset();
			}
			return result;
		}
		else
		{
			return DD_OK;
		}
	}

	template <typename TSurface>
	HRESULT SurfaceImpl<TSurface>::Unlock(TSurface* This, TUnlockParam lpRect)
	{
		return getOrigVtable(This).Unlock(This, lpRect);
	}

	template <typename TSurface>
	void SurfaceImpl<TSurface>::restoreOrigCaps(DWORD& caps)
	{
		caps |= m_data->m_origCaps & (DDSCAPS_3DDEVICE | DDSCAPS_MIPMAP | DDSCAPS_COMPLEX);
	}

	template SurfaceImpl<IDirectDrawSurface>;
	template SurfaceImpl<IDirectDrawSurface2>;
	template SurfaceImpl<IDirectDrawSurface3>;
	template SurfaceImpl<IDirectDrawSurface4>;
	template SurfaceImpl<IDirectDrawSurface7>;

	void setBltSrc(D3DDDIARG_BLT& data)
	{
		if (g_bltSrcResource)
		{
			data.hSrcResource = g_bltSrcResource;
			data.SrcSubResourceIndex = g_bltSrcSubResourceIndex;
			OffsetRect(&data.SrcRect, g_bltSrcRect.left, g_bltSrcRect.top);
		}
	}
}
