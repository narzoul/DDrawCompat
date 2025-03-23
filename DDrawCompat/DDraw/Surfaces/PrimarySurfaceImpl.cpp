#include <Common/CompatPtr.h>
#include <Config/Settings/FpsLimiter.h>
#include <Config/Settings/VSync.h>
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
		HRESULT result = SurfaceImpl::Blt(This, lpDestRect, lpDDSrcSurface, lpSrcRect, dwFlags, lpDDBltFx);
		if (SUCCEEDED(result))
		{
			bltToGdi(This, lpDestRect, lpDDSrcSurface, lpSrcRect, dwFlags, lpDDBltFx);
			auto statsWindow = Gdi::GuiThread::getStatsWindow();
			if (statsWindow)
			{
				statsWindow->m_blit.add();
			}
			RealPrimarySurface::scheduleUpdate();
			PrimarySurface::waitForIdle();
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
		HRESULT result = SurfaceImpl::BltFast(This, dwX, dwY, lpDDSrcSurface, lpSrcRect, dwTrans);
		if (SUCCEEDED(result))
		{
			auto statsWindow = Gdi::GuiThread::getStatsWindow();
			if (statsWindow)
			{
				statsWindow->m_blit.add();
			}
			RealPrimarySurface::scheduleUpdate();
			PrimarySurface::waitForIdle();
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

		if (Config::Settings::VSync::WAIT == Config::vSync.get())
		{
			static UINT lastFlipEnd = 0;
			lastFlipEnd += Config::vSync.getParam();
			UINT vsyncCount = D3dDdi::KernelModeThunks::getVsyncCounter();
			if (static_cast<INT>(vsyncCount - lastFlipEnd) > 0)
			{
				lastFlipEnd = vsyncCount;
			}

			RealPrimarySurface::setUpdateReady();
			if (0 != RealPrimarySurface::flush())
			{
				PrimarySurface::waitForIdle();
			}

			while (static_cast<INT>(vsyncCount - lastFlipEnd) < 0)
			{
				++vsyncCount;
				D3dDdi::KernelModeThunks::waitForVsyncCounter(vsyncCount);
				RealPrimarySurface::flush();
			}
		}
		else
		{
			PrimarySurface::waitForIdle();
		}

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
			RealPrimarySurface::scheduleUpdate();
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
			RealPrimarySurface::scheduleUpdate();
		}
		return result;
	}

	template PrimarySurfaceImpl<IDirectDrawSurface>;
	template PrimarySurfaceImpl<IDirectDrawSurface2>;
	template PrimarySurfaceImpl<IDirectDrawSurface3>;
	template PrimarySurfaceImpl<IDirectDrawSurface4>;
	template PrimarySurfaceImpl<IDirectDrawSurface7>;
}
