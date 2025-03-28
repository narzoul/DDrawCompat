#include <Common/CompatPtr.h>
#include <Config/Settings/FpsLimiter.h>
#include <D3dDdi/KernelModeThunks.h>
#include <D3dDdi/ScopedCriticalSection.h>
#include <DDraw/DirectDrawClipper.h>
#include <DDraw/DirectDrawPalette.h>
#include <DDraw/DirectDrawSurface.h>
#include <DDraw/RealPrimarySurface.h>
#include <DDraw/Surfaces/PrimarySurface.h>
#include <DDraw/Surfaces/PrimarySurfaceImpl.h>
#include <Dll/Dll.h>
#include <Gdi/Gdi.h>
#include <Gdi/GuiThread.h>
#include <Gdi/Region.h>
#include <Gdi/VirtualScreen.h>
#include <Overlay/StatsWindow.h>

namespace
{
	template <typename TSurface>
	void bltToGdi(TSurface* This, LPRECT lpDestRect, TSurface* lpDDSrcSurface, LPRECT lpSrcRect,
		DWORD dwFlags, LPDDBLTFX lpDDBltFx)
	{
		if (!lpDestRect || DDraw::RealPrimarySurface::isFullscreen())
		{
			return;
		}

		CompatPtr<IDirectDrawClipper> clipper;
		CompatVtable<Vtable<TSurface>>::s_origVtable.GetClipper(This, &clipper.getRef());
		if (!clipper)
		{
			return;
		}

		D3dDdi::ScopedCriticalSection lock;
		Gdi::Region clipRgn(DDraw::DirectDrawClipper::getClipRgn(*clipper));
		RECT monitorRect = DDraw::PrimarySurface::getMonitorInfo().rcEmulated;
		RECT virtualScreenBounds = Gdi::VirtualScreen::getBounds();
		clipRgn.offset(monitorRect.left, monitorRect.top);
		clipRgn &= virtualScreenBounds;
		clipRgn -= monitorRect;
		if (clipRgn.isEmpty())
		{
			return;
		}

		auto gdiSurface(Gdi::VirtualScreen::createSurface(virtualScreenBounds));
		if (!gdiSurface)
		{
			return;
		}

		CompatPtr<IDirectDrawClipper> gdiClipper;
		CALL_ORIG_PROC(DirectDrawCreateClipper)(0, &gdiClipper.getRef(), nullptr);
		if (!gdiClipper)
		{
			return;
		}

		RECT dstRect = *lpDestRect;
		OffsetRect(&dstRect, monitorRect.left - virtualScreenBounds.left, monitorRect.top - virtualScreenBounds.top);
		clipRgn.offset(-virtualScreenBounds.left, -virtualScreenBounds.top);
		DDraw::DirectDrawClipper::setClipRgn(*gdiClipper, clipRgn);

		auto srcSurface(CompatPtr<IDirectDrawSurface7>::from(lpDDSrcSurface));
		gdiSurface->SetClipper(gdiSurface, gdiClipper);
		gdiSurface.get()->lpVtbl->Blt(gdiSurface, &dstRect, srcSurface, lpSrcRect, dwFlags, lpDDBltFx);
		gdiSurface->SetClipper(gdiSurface, nullptr);
	}

	bool isFsBlt(int cx, int cy)
	{
		RECT monitorRect = DDraw::PrimarySurface::getMonitorInfo().rcEmulated;
		return monitorRect.right - monitorRect.left == cx &&
			monitorRect.bottom - monitorRect.top == cy;
	}

	bool isFsBlt(LPRECT lpDestRect)
	{
		return !lpDestRect || 0 == lpDestRect->left && 0 == lpDestRect->top &&
			isFsBlt(lpDestRect->right, lpDestRect->bottom);
	}

	template <typename TSurface>
	bool isFsBltFast(DWORD dwX, DWORD dwY, TSurface* lpDDSrcSurface, LPRECT lpSrcRect)
	{
		if (0 != dwX || 0 != dwY || !lpDDSrcSurface)
		{
			return false;
		}

		if (lpSrcRect)
		{
			return isFsBlt(lpSrcRect->right - lpSrcRect->left, lpSrcRect->bottom - lpSrcRect->top);
		}

		DDraw::Types<TSurface>::TSurfaceDesc desc = {};
		desc.dwSize = sizeof(desc);
		getOrigVtable(lpDDSrcSurface).GetSurfaceDesc(lpDDSrcSurface, &desc);
		return isFsBlt(desc.dwWidth, desc.dwHeight);
	}

	void restorePrimaryCaps(DWORD& caps)
	{
		caps &= ~DDSCAPS_OFFSCREENPLAIN;
		caps |= DDSCAPS_PRIMARYSURFACE | DDSCAPS_VISIBLE;
	}
}

namespace DDraw
{
	template <typename TSurface>
	PrimarySurfaceImpl<TSurface>::PrimarySurfaceImpl(Surface* data)
		: SurfaceImpl(data)
	{
	}

	template <typename TSurface>
	HRESULT PrimarySurfaceImpl<TSurface>::AddAttachedSurface(TSurface* This, TSurface* lpDDSAttachedSurface)
	{
		HRESULT result = getOrigVtable(This).AddAttachedSurface(This, lpDDSAttachedSurface);
		if (SUCCEEDED(result) && !(PrimarySurface::getOrigCaps() & DDSCAPS_3DDEVICE))
		{
			TDdsCaps caps = {};
			getOrigVtable(This).GetCaps(lpDDSAttachedSurface, &caps);
			if (caps.dwCaps & DDSCAPS_3DDEVICE)
			{
				PrimarySurface::setAsRenderTarget();
			}
		}
		return result;
	}

	template <typename TSurface>
	HRESULT PrimarySurfaceImpl<TSurface>::Blt(
		TSurface* This, LPRECT lpDestRect, TSurface* lpDDSrcSurface, LPRECT lpSrcRect,
		DWORD dwFlags, LPDDBLTFX lpDDBltFx)
	{
		if (RealPrimarySurface::isLost())
		{
			return DDERR_SURFACELOST;
		}

		RealPrimarySurface::flush();
		if (Config::Settings::FpsLimiter::FLIPSTART == Config::fpsLimiter.get() && isFsBlt(lpDestRect))
		{
			RealPrimarySurface::waitForFlipFpsLimit();
		}
		HRESULT result = SurfaceImpl::Blt(This, lpDestRect, lpDDSrcSurface, lpSrcRect, dwFlags, lpDDBltFx);
		if (SUCCEEDED(result))
		{
			bltToGdi(This, lpDestRect, lpDDSrcSurface, lpSrcRect, dwFlags, lpDDBltFx);
			auto statsWindow = Gdi::GuiThread::getStatsWindow();
			if (statsWindow)
			{
				statsWindow->m_blit.add();
			}
			RealPrimarySurface::scheduleUpdate(true);
		}
		if (Config::Settings::FpsLimiter::FLIPEND == Config::fpsLimiter.get() && isFsBlt(lpDestRect))
		{
			RealPrimarySurface::waitForFlipFpsLimit();
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

		RealPrimarySurface::flush();
		if (Config::Settings::FpsLimiter::FLIPSTART == Config::fpsLimiter.get()
			&& isFsBltFast(dwX, dwY, lpDDSrcSurface, lpSrcRect))
		{
			RealPrimarySurface::waitForFlipFpsLimit();
		}
		HRESULT result = SurfaceImpl::BltFast(This, dwX, dwY, lpDDSrcSurface, lpSrcRect, dwTrans);
		if (SUCCEEDED(result))
		{
			auto statsWindow = Gdi::GuiThread::getStatsWindow();
			if (statsWindow)
			{
				statsWindow->m_blit.add();
			}
			RealPrimarySurface::scheduleUpdate(true);
		}
		if (Config::Settings::FpsLimiter::FLIPEND == Config::fpsLimiter.get()
			&& isFsBltFast(dwX, dwY, lpDDSrcSurface, lpSrcRect))
		{
			RealPrimarySurface::waitForFlipFpsLimit();
		}
		return result;
	}

	template <typename TSurface>
	HRESULT PrimarySurfaceImpl<TSurface>::Flip(TSurface* This, TSurface* lpDDSurfaceTargetOverride, DWORD dwFlags)
	{
		if (!RealPrimarySurface::isFullscreen())
		{
			return DDERR_NOEXCLUSIVEMODE;
		}

		RealPrimarySurface::setUpdateReady();
		RealPrimarySurface::flush();
		RealPrimarySurface::waitForFlip(m_data->getDDS());

		if (Config::Settings::FpsLimiter::FLIPSTART == Config::fpsLimiter.get())
		{
			RealPrimarySurface::waitForFlipFpsLimit();
		}

		auto surfaceTargetOverride(CompatPtr<TSurface>::from(lpDDSurfaceTargetOverride));
		const bool isFlipEmulated = 0 != (PrimarySurface::getOrigCaps() & DDSCAPS_SYSTEMMEMORY);
		if (isFlipEmulated)
		{
			if (!surfaceTargetOverride)
			{
				TDdsCaps caps = {};
				caps.dwCaps = DDSCAPS_BACKBUFFER;
				getOrigVtable(This).GetAttachedSurface(This, &caps, &surfaceTargetOverride.getRef());
			}

			HRESULT result = SurfaceImpl::Blt(This, nullptr, surfaceTargetOverride.get(), nullptr, DDBLT_WAIT, nullptr);
			if (FAILED(result))
			{
				return result;
			}

			dwFlags = DDFLIP_NOVSYNC;
		}
		else
		{
			HRESULT result = SurfaceImpl::Flip(This, surfaceTargetOverride, DDFLIP_WAIT);
			if (FAILED(result))
			{
				return result;
			}
		}

		auto statsWindow = Gdi::GuiThread::getStatsWindow();
		if (statsWindow)
		{
			statsWindow->m_flip.add();
		}

		PrimarySurface::updateFrontResource();
		RealPrimarySurface::flip(surfaceTargetOverride, dwFlags);
		PrimarySurface::waitForIdle();

		if (Config::Settings::FpsLimiter::FLIPEND == Config::fpsLimiter.get())
		{
			RealPrimarySurface::waitForFlipFpsLimit();
		}
		return DD_OK;
	}

	template <typename TSurface>
	HRESULT PrimarySurfaceImpl<TSurface>::GetAttachedSurface(TSurface* This, TDdsCaps* lpDDSCaps, TSurface** lplpDDAttachedSurface)
	{
		if (lpDDSCaps && (m_data->getOrigCaps() & DDSCAPS_SYSTEMMEMORY))
		{
			TDdsCaps caps = *lpDDSCaps;
			caps.dwCaps &= ~DDSCAPS_SYSTEMMEMORY;
			return SurfaceImpl::GetAttachedSurface(This, &caps, lplpDDAttachedSurface);
		}
		return SurfaceImpl::GetAttachedSurface(This, lpDDSCaps, lplpDDAttachedSurface);
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
	HRESULT PrimarySurfaceImpl<TSurface>::GetDC(TSurface* This, HDC* lphDC)
	{
		if (RealPrimarySurface::isLost())
		{
			return DDERR_SURFACELOST;
		}

		RealPrimarySurface::flush();
		HRESULT result = SurfaceImpl::GetDC(This, lphDC);
		if (SUCCEEDED(result))
		{
			auto statsWindow = Gdi::GuiThread::getStatsWindow();
			if (statsWindow)
			{
				statsWindow->m_lock.add();
			}
			RealPrimarySurface::scheduleUpdate();
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

		RealPrimarySurface::flush();
		HRESULT result = SurfaceImpl::Lock(This, lpDestRect, lpDDSurfaceDesc, dwFlags, hEvent);
		if (SUCCEEDED(result))
		{
			restorePrimaryCaps(lpDDSurfaceDesc->ddsCaps.dwCaps);
			auto statsWindow = Gdi::GuiThread::getStatsWindow();
			if (statsWindow)
			{
				statsWindow->m_lock.add();
			}
			RealPrimarySurface::scheduleUpdate();
		}
		return result;
	}

	template <typename TSurface>
	HRESULT PrimarySurfaceImpl<TSurface>::ReleaseDC(TSurface* This, HDC hDC)
	{
		HRESULT result = SurfaceImpl::ReleaseDC(This, hDC);
		if (SUCCEEDED(result))
		{
			RealPrimarySurface::scheduleUpdate(true);
		}
		return result;
	}

	template <typename TSurface>
	HRESULT PrimarySurfaceImpl<TSurface>::Restore(TSurface* This)
	{
		HRESULT result = IsLost(This);
		if (FAILED(result))
		{
			auto realPrimary = RealPrimarySurface::getSurface();
			result = SUCCEEDED(realPrimary->IsLost(realPrimary)) ? DD_OK : RealPrimarySurface::restore();
			if (SUCCEEDED(result))
			{
				return SurfaceImpl::Restore(This);
			}
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

		HRESULT result = SurfaceImpl::SetPalette(This, lpDDPalette);
		if (SUCCEEDED(result))
		{
			PrimarySurface::s_palette = lpDDPalette;
			PrimarySurface::updatePalette();
		}
		return result;
	}

	template <typename TSurface>
	HRESULT PrimarySurfaceImpl<TSurface>::Unlock(TSurface* This, TUnlockParam lpRect)
	{
		HRESULT result = SurfaceImpl::Unlock(This, lpRect);
		if (SUCCEEDED(result))
		{
			RealPrimarySurface::scheduleUpdate(true);
		}
		return result;
	}

	template PrimarySurfaceImpl<IDirectDrawSurface>;
	template PrimarySurfaceImpl<IDirectDrawSurface2>;
	template PrimarySurfaceImpl<IDirectDrawSurface3>;
	template PrimarySurfaceImpl<IDirectDrawSurface4>;
	template PrimarySurfaceImpl<IDirectDrawSurface7>;
}
