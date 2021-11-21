#include <map>
#include <sstream>

#include <Common/Comparison.h>
#include <Common/CompatPtr.h>
#include <Common/CompatVtable.h>
#include <D3dDdi/Adapter.h>
#include <D3dDdi/KernelModeThunks.h>
#include <DDraw/DirectDraw.h>
#include <DDraw/RealPrimarySurface.h>
#include <DDraw/ScopedThreadLock.h>
#include <DDraw/Surfaces/PrimarySurface.h>
#include <DDraw/Visitors/DirectDrawVtblVisitor.h>

namespace
{
	void logSrcColorKeySupportFailure(const char* reason, UINT32 resultCode);

	bool checkSrcColorKeySupport(CompatRef<IDirectDraw7> dd)
	{
		DDCAPS caps = {};
		caps.dwSize = sizeof(caps);
		dd->GetCaps(&dd, &caps, nullptr);
		if (!(caps.dwCaps & DDCAPS_COLORKEY) || !(caps.dwCKeyCaps & DDCKEYCAPS_SRCBLT))
		{
			logSrcColorKeySupportFailure("driver indicates no support", 0);
			return false;
		}

		DDSURFACEDESC2 desc = {};
		desc.dwSize = sizeof(desc);
		desc.dwFlags = DDSD_WIDTH | DDSD_HEIGHT | DDSD_PIXELFORMAT | DDSD_CAPS;
		desc.dwWidth = 2;
		desc.dwHeight = 1;
		desc.ddpfPixelFormat = DDraw::DirectDraw::getRgbPixelFormat(16);
		desc.ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN | DDSCAPS_VIDEOMEMORY;

		CompatPtr<IDirectDrawSurface7> src;
		HRESULT result = dd->CreateSurface(&dd, &desc, &src.getRef(), nullptr);
		if (FAILED(result))
		{
			logSrcColorKeySupportFailure("error creating source surface", result);
			return false;
		}

		CompatPtr<IDirectDrawSurface7> dst;
		result = dd->CreateSurface(&dd, &desc, &dst.getRef(), nullptr);
		if (FAILED(result))
		{
			logSrcColorKeySupportFailure("error creating destination surface", result);
			return false;
		}

		result = src->Lock(src, nullptr, &desc, DDLOCK_WAIT, nullptr);
		if (FAILED(result))
		{
			logSrcColorKeySupportFailure("error locking source surface", result);
			return false;
		}

		const UINT16 colorKey = 0xFA9F;
		*static_cast<UINT32*>(desc.lpSurface) = colorKey;
		src->Unlock(src, nullptr);

		result = dst->Lock(dst, nullptr, &desc, DDLOCK_WAIT, nullptr);
		if (FAILED(result))
		{
			logSrcColorKeySupportFailure("error locking destination surface", result);
			return false;
		}

		*static_cast<UINT32*>(desc.lpSurface) = 0xFFFFFFFF;
		dst->Unlock(dst, nullptr);

		DDBLTFX fx = {};
		fx.dwSize = sizeof(fx);
		fx.ddckSrcColorkey.dwColorSpaceLowValue = colorKey;
		fx.ddckSrcColorkey.dwColorSpaceHighValue = colorKey;
		result = dst->Blt(dst, nullptr, src, nullptr, DDBLT_KEYSRCOVERRIDE | DDBLT_WAIT, &fx);
		if (FAILED(result))
		{
			logSrcColorKeySupportFailure("blt error", result);
			return false;
		}

		result = dst->Lock(dst, nullptr, &desc, DDLOCK_WAIT, nullptr);
		if (FAILED(result))
		{
			logSrcColorKeySupportFailure("error locking destination resource after blt", result);
			return false;
		}

		const UINT32 dstPixels = *static_cast<UINT32*>(desc.lpSurface);
		dst->Unlock(dst, nullptr);
		
		if (dstPixels != 0xFFFF)
		{
			logSrcColorKeySupportFailure("test result pattern is incorrect", dstPixels);
			return false;
		}

		Compat::Log() << "Source color key support: yes";
		return true;
	}

	void logSrcColorKeySupportFailure(const char* reason, UINT32 resultCode)
	{
		Compat::Log log;
		log << "Source color key support: no (" << reason;
		if (resultCode)
		{
			log << ": " << Compat::hex(resultCode);
		}
		log << ')';
	}

	template <typename TDirectDraw, typename TSurfaceDesc, typename TSurface>
	HRESULT STDMETHODCALLTYPE CreateSurface(
		TDirectDraw* This, TSurfaceDesc* lpDDSurfaceDesc, TSurface** lplpDDSurface, IUnknown* pUnkOuter)
	{
		if (!This || !lpDDSurfaceDesc || !lplpDDSurface)
		{
			return getOrigVtable(This).CreateSurface(This, lpDDSurfaceDesc, lplpDDSurface, pUnkOuter);
		}

		if (lpDDSurfaceDesc->ddsCaps.dwCaps & DDSCAPS_PRIMARYSURFACE)
		{
			return DDraw::PrimarySurface::create<TDirectDraw>(*This, *lpDDSurfaceDesc, *lplpDDSurface);
		}
		else
		{
			return DDraw::Surface::create<TDirectDraw>(
				*This, *lpDDSurfaceDesc, *lplpDDSurface, std::make_unique<DDraw::Surface>(lpDDSurfaceDesc->ddsCaps.dwCaps));
		}
	}

	template <typename TDirectDraw>
	HRESULT STDMETHODCALLTYPE FlipToGDISurface(TDirectDraw* /*This*/)
	{
		return DDraw::PrimarySurface::flipToGdiSurface();
	}

	template <typename TDirectDraw, typename TSurface>
	HRESULT STDMETHODCALLTYPE GetGDISurface(TDirectDraw* /*This*/, TSurface** lplpGDIDDSSurface)
	{
		if (!lplpGDIDDSSurface)
		{
			return DDERR_INVALIDPARAMS;
		}

		auto gdiSurface(DDraw::PrimarySurface::getGdiSurface());
		if (!gdiSurface)
		{
			return DDERR_NOTFOUND;
		}

		*lplpGDIDDSSurface = CompatPtr<TSurface>::from(gdiSurface.get()).detach();
		return DD_OK;
	}

	template <typename TDirectDraw>
	HRESULT STDMETHODCALLTYPE RestoreAllSurfaces(TDirectDraw* This)
	{
		auto primary(DDraw::PrimarySurface::getPrimary());
		if (primary)
		{
			primary.get()->lpVtbl->Restore(primary);
		}
		return getOrigVtable(This).RestoreAllSurfaces(This);
	}

	template <typename TDirectDraw>
	HRESULT STDMETHODCALLTYPE WaitForVerticalBlank(TDirectDraw* This, DWORD dwFlags, HANDLE hEvent)
	{
		DDraw::RealPrimarySurface::flush();
		return getOrigVtable(This).WaitForVerticalBlank(This, dwFlags, hEvent);
	}

	template <typename Vtable>
	constexpr void setCompatVtable(Vtable& vtable)
	{
		vtable.CreateSurface = &CreateSurface;
		vtable.FlipToGDISurface = &FlipToGDISurface;
		vtable.GetGDISurface = &GetGDISurface;
		vtable.WaitForVerticalBlank = &WaitForVerticalBlank;

		if constexpr (std::is_same_v<Vtable, IDirectDraw4Vtbl> || std::is_same_v<Vtable, IDirectDraw7Vtbl>)
		{
			vtable.RestoreAllSurfaces = &RestoreAllSurfaces;
		}
	}
}

namespace DDraw
{
	namespace DirectDraw
	{
		DDSURFACEDESC2 getDisplayMode(CompatRef<IDirectDraw7> dd)
		{
			DDSURFACEDESC2 dm = {};
			dm.dwSize = sizeof(dm);
			dd->GetDisplayMode(&dd, &dm);
			return dm;
		}

		DDPIXELFORMAT getRgbPixelFormat(DWORD bpp)
		{
			DDPIXELFORMAT pf = {};
			pf.dwSize = sizeof(pf);
			pf.dwFlags = DDPF_RGB;
			pf.dwRGBBitCount = bpp;

			switch (bpp)
			{
			case 1:
				pf.dwFlags |= DDPF_PALETTEINDEXED1;
				break;
			case 2:
				pf.dwFlags |= DDPF_PALETTEINDEXED2;
				break;
			case 4:
				pf.dwFlags |= DDPF_PALETTEINDEXED4;
				break;
			case 8:
				pf.dwFlags |= DDPF_PALETTEINDEXED8;
				break;
			case 16:
				pf.dwRBitMask = 0xF800;
				pf.dwGBitMask = 0x07E0;
				pf.dwBBitMask = 0x001F;
				break;
			case 24:
			case 32:
				pf.dwRBitMask = 0xFF0000;
				pf.dwGBitMask = 0x00FF00;
				pf.dwBBitMask = 0x0000FF;
				break;
			}

			return pf;
		}

		void onCreate(GUID* guid, CompatRef<IDirectDraw7> dd)
		{
			static std::map<LUID, Repository> repositories;
			auto adapterInfo = D3dDdi::KernelModeThunks::getAdapterInfo(dd);
			auto it = repositories.find(adapterInfo.luid);
			if (it == repositories.end())
			{
				CompatPtr<IDirectDraw7> repo;
				CALL_ORIG_PROC(DirectDrawCreateEx)(guid, reinterpret_cast<void**>(&repo.getRef()), IID_IDirectDraw7, nullptr);
				if (!repo)
				{
					return;
				}
				repo->SetCooperativeLevel(repo, nullptr, DDSCL_NORMAL);
				it = repositories.insert({ adapterInfo.luid, { repo, checkSrcColorKeySupport(*repo) } }).first;
				repo.detach();
			}
			D3dDdi::Adapter::setRepository(adapterInfo.luid, it->second);
		}

		void suppressEmulatedDirectDraw(GUID*& guid)
		{
			if (reinterpret_cast<GUID*>(DDCREATE_EMULATIONONLY) == guid)
			{
				LOG_ONCE("Suppressed a request to create an emulated DirectDraw object");
				guid = nullptr;
			}
		}

		template <typename Vtable>
		void hookVtable(const Vtable& vtable)
		{
			CompatVtable<Vtable>::hookVtable<ScopedThreadLock>(vtable);
		}

		template void hookVtable(const IDirectDrawVtbl&);
		template void hookVtable(const IDirectDraw2Vtbl&);
		template void hookVtable(const IDirectDraw4Vtbl&);
		template void hookVtable(const IDirectDraw7Vtbl&);
	}
}
