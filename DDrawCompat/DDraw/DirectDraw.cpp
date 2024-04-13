#include <map>
#include <set>

#include <Common/Comparison.h>
#include <Common/CompatPtr.h>
#include <Common/CompatVtable.h>
#include <Common/Log.h>
#include <Config/Settings/AltTabFix.h>
#include <Config/Settings/PalettizedTextures.h>
#include <Config/Settings/SoftwareDevice.h>
#include <D3dDdi/Adapter.h>
#include <D3dDdi/Device.h>
#include <D3dDdi/KernelModeThunks.h>
#include <D3dDdi/Resource.h>
#include <D3dDdi/SurfaceRepository.h>
#include <DDraw/DirectDraw.h>
#include <DDraw/DirectDrawSurface.h>
#include <DDraw/LogUsedResourceFormat.h>
#include <DDraw/RealPrimarySurface.h>
#include <DDraw/ScopedThreadLock.h>
#include <DDraw/Surfaces/PalettizedTexture.h>
#include <DDraw/Surfaces/PrimarySurface.h>
#include <DDraw/Surfaces/TagSurface.h>
#include <DDraw/Visitors/DirectDrawVtblVisitor.h>
#include <Gdi/WinProc.h>
#include <Win32/DisplayMode.h>
#include <Win32/Thread.h>

namespace
{
	WNDPROC g_origDDrawWindowProc = nullptr;

	LRESULT handleActivateApp(HWND hwnd, WPARAM wParam, LPARAM lParam, WNDPROC origWndProc);
	LRESULT handleSize(HWND hwnd, WPARAM wParam, LPARAM lParam);

	template <typename TDirectDraw, typename TSurfaceDesc, typename TSurface>
	HRESULT STDMETHODCALLTYPE CreateSurface(
		TDirectDraw* This, TSurfaceDesc* lpDDSurfaceDesc, TSurface** lplpDDSurface, IUnknown* pUnkOuter)
	{
		if (!This || !lpDDSurfaceDesc || !lplpDDSurface)
		{
			return getOrigVtable(This).CreateSurface(This, lpDDSurfaceDesc, lplpDDSurface, pUnkOuter);
		}

		DDSURFACEDESC2 desc2 = {};
		memcpy(&desc2, lpDDSurfaceDesc, sizeof(*lpDDSurfaceDesc));
		DDraw::LogUsedResourceFormat logUsedResourceFormat(desc2, reinterpret_cast<IUnknown*&>(*lplpDDSurface));

		if (lpDDSurfaceDesc->ddsCaps.dwCaps & DDSCAPS_PRIMARYSURFACE)
		{
			return DDraw::PrimarySurface::create<TDirectDraw>(*This, *lpDDSurfaceDesc, *lplpDDSurface);
		}

		TSurfaceDesc desc = *lpDDSurfaceDesc;
		if (!D3dDdi::SurfaceRepository::inCreateSurface())
		{
			const bool isPalettized = (desc.dwFlags & DDSD_PIXELFORMAT)
				? (desc.ddpfPixelFormat.dwFlags & DDPF_PALETTEINDEXED8)
				: (Win32::DisplayMode::getBpp() <= 8);

			if (&IID_IDirect3DHALDevice == Config::softwareDevice.get())
			{
				if ((desc.ddsCaps.dwCaps & DDSCAPS_SYSTEMMEMORY) &&
					!(desc.ddsCaps.dwCaps & DDSCAPS_3DDEVICE) &&
					isPalettized)
				{
					desc.ddsCaps.dwCaps |= DDSCAPS_TEXTURE;
					desc.ddsCaps.dwCaps &= ~DDSCAPS_OFFSCREENPLAIN;
				}

				if (desc.ddsCaps.dwCaps & DDSCAPS_SYSTEMMEMORY &&
					(desc.ddsCaps.dwCaps & (DDSCAPS_TEXTURE | DDSCAPS_3DDEVICE | DDSCAPS_ZBUFFER)))
				{
					desc.ddsCaps.dwCaps &= ~DDSCAPS_SYSTEMMEMORY;
					desc.ddsCaps.dwCaps |= DDSCAPS_VIDEOMEMORY;
				}
			}

			if (isPalettized && (desc.ddsCaps.dwCaps & DDSCAPS_TEXTURE))
			{
				if (Config::palettizedTextures.get())
				{
					if (!(desc.ddsCaps.dwCaps & DDSCAPS_SYSTEMMEMORY))
					{
						return DDraw::PalettizedTexture::create<TDirectDraw>(*This, desc, *lplpDDSurface);
					}
				}
				else if (desc.ddsCaps.dwCaps & DDSCAPS_VIDEOMEMORY)
				{
					return DDERR_UNSUPPORTED;
				}
				else
				{
					desc.ddsCaps.dwCaps |= DDSCAPS_SYSTEMMEMORY;
				}
			}
		}

		return DDraw::Surface::create<TDirectDraw>(*This, desc, *lplpDDSurface,
			std::make_unique<DDraw::Surface>(desc.dwFlags, lpDDSurfaceDesc->ddsCaps.dwCaps));
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
	HRESULT STDMETHODCALLTYPE SetCooperativeLevel(TDirectDraw* This, HWND hWnd, DWORD dwFlags)
	{
		HRESULT result = getOrigVtable(This).SetCooperativeLevel(This, hWnd, dwFlags);
		const bool isFullscreen = (dwFlags & DDSCL_FULLSCREEN) && (dwFlags & DDSCL_EXCLUSIVE);
		if (SUCCEEDED(result) && (isFullscreen || (dwFlags & DDSCL_NORMAL)))
		{
			auto tagSurface = DDraw::TagSurface::get(*CompatPtr<IDirectDraw>::from(This));
			if (tagSurface)
			{
				const bool wasFullscreen = tagSurface->isFullscreen();
				if (wasFullscreen != isFullscreen)
				{
					tagSurface->setFullscreenWindow(isFullscreen ? hWnd : nullptr);
				}
			}
		}
		return result;
	}

	template <typename TDirectDraw>
	HRESULT STDMETHODCALLTYPE WaitForVerticalBlank(TDirectDraw* This, DWORD dwFlags, HANDLE hEvent)
	{
		DDraw::RealPrimarySurface::setUpdateReady();
		DDraw::RealPrimarySurface::flush();
		return getOrigVtable(This).WaitForVerticalBlank(This, dwFlags, hEvent);
	}

	LRESULT WINAPI ddrawWindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
	{
		LOG_FUNC("ddrawWindowProc", hwnd, uMsg, wParam, lParam);
		switch (uMsg)
		{
		case WM_ACTIVATEAPP:
			return LOG_RESULT(handleActivateApp(hwnd, wParam, lParam, Gdi::WinProc::getDDrawOrigWndProc(hwnd)));
		case WM_SIZE:
			return LOG_RESULT(handleSize(hwnd, wParam, lParam));
		}
		return LOG_RESULT(g_origDDrawWindowProc(hwnd, uMsg, wParam, lParam));
	}

	LRESULT handleActivateApp(HWND hwnd, WPARAM wParam, LPARAM lParam, WNDPROC origWndProc)
	{
		LOG_FUNC("DirectDraw::handleActivateApp", hwnd, wParam, lParam, origWndProc);

		if (origWndProc)
		{
			auto tagSurface = DDraw::TagSurface::findFullscreenWindow();
			if (tagSurface && tagSurface->getExclusiveOwnerThreadId() != GetCurrentThreadId())
			{
				if (!wParam)
				{
					ShowWindow(hwnd, SW_SHOWMINNOACTIVE);
				}
				return LOG_RESULT(CallWindowProcA(origWndProc, hwnd, WM_ACTIVATEAPP, wParam, lParam));
			}
		}

		if (Config::Settings::AltTabFix::OFF == Config::altTabFix.get())
		{
			return LOG_RESULT(g_origDDrawWindowProc(hwnd, WM_ACTIVATEAPP, wParam, lParam));
		}

		DDraw::ScopedThreadLock lock;
		const bool keepPrimary = Config::Settings::AltTabFix::KEEPVIDMEM == Config::altTabFix.get();
		std::set<DDRAWI_DDRAWSURFACE_LCL*> surfacesToRestore;
		DDraw::Surface::enumSurfaces([&](const DDraw::Surface& surface)
			{
				auto lcl = DDraw::DirectDrawSurface::getInt(*surface.getDDS()).lpLcl;
				if (!(lcl->dwFlags & DDRAWISURF_INVALID) &&
					(keepPrimary || !(surface.getOrigCaps() & DDSCAPS_PRIMARYSURFACE)))
				{
					lcl->dwFlags |= DDRAWISURF_INVALID;
					surfacesToRestore.insert(lcl);
				}
			});

		LRESULT result = g_origDDrawWindowProc(hwnd, WM_ACTIVATEAPP, wParam, lParam);

		DDraw::Surface::enumSurfaces([&](const DDraw::Surface& surface)
			{
				auto lcl = DDraw::DirectDrawSurface::getInt(*surface.getDDS()).lpLcl;
				auto it = surfacesToRestore.find(lcl);
				if (it != surfacesToRestore.end())
				{
					lcl->dwFlags &= ~DDRAWISURF_INVALID;
					surfacesToRestore.erase(it);
				}
			});

		if (wParam && keepPrimary)
		{
			auto realPrimary(DDraw::RealPrimarySurface::getSurface());
			if (realPrimary)
			{
				realPrimary->Restore(realPrimary);
				auto gdiResource = DDraw::PrimarySurface::getGdiResource();
				if (gdiResource)
				{
					D3dDdi::Device::setGdiResourceHandle(gdiResource);
				}
			}
		}

		return LOG_RESULT(result);
	}

	LRESULT handleSize(HWND hwnd, WPARAM wParam, LPARAM lParam)
	{
		LOG_FUNC("DirectDraw::handleSize", hwnd, wParam, lParam);
		LRESULT result = 0;
		auto tagSurface = DDraw::TagSurface::findFullscreenWindow();
		if (tagSurface && tagSurface->getExclusiveOwnerThreadId() != GetCurrentThreadId())
		{
			Win32::Thread::skipWaitingForExclusiveModeMutex(true);
		}
		result = g_origDDrawWindowProc(hwnd, WM_SIZE, wParam, lParam);
		Win32::Thread::skipWaitingForExclusiveModeMutex(false);
		return LOG_RESULT(result);
	}

	template <typename Vtable>
	constexpr void setCompatVtable(Vtable& vtable)
	{
		vtable.CreateSurface = &CreateSurface;
		vtable.FlipToGDISurface = &FlipToGDISurface;
		vtable.GetGDISurface = &GetGDISurface;
		vtable.SetCooperativeLevel = &SetCooperativeLevel;
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

		void hookDDrawWindowProc(WNDPROC ddrawWndProc)
		{
			LOG_FUNC("DirectDraw::hookDDrawWindowProc", ddrawWndProc);
			static bool isHooked = false;
			if (isHooked)
			{
				return;
			}
			isHooked = true;

			g_origDDrawWindowProc = ddrawWndProc;
			Compat::hookFunction(reinterpret_cast<void*&>(g_origDDrawWindowProc), ddrawWindowProc, "ddrawWindowProc");
			Compat::closeDbgEng();
		}

		void onCreate(GUID* guid, CompatRef<IDirectDraw7> dd)
		{
			static std::map<LUID, CompatWeakPtr<IDirectDraw7>> repositories;
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
				repo.get()->lpVtbl->SetCooperativeLevel(repo, nullptr, DDSCL_NORMAL);
				it = repositories.insert({ adapterInfo.luid, repo }).first;
				D3dDdi::Adapter::setRepository(adapterInfo.luid, guid, it->second);
				repo.detach();
			}
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
