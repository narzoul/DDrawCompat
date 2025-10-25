#include <map>
#include <set>

#include <Common/Comparison.h>
#include <Common/CompatPtr.h>
#include <Common/CompatVtable.h>
#include <Common/Log.h>
#include <Common/ScopedCriticalSection.h>
#include <Config/Settings/AltTabFix.h>
#include <Config/Settings/CapsPatches.h>
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
#include <DDraw/Surfaces/PrimarySurface.h>
#include <DDraw/Surfaces/TagSurface.h>
#include <DDraw/Visitors/DirectDrawVtblVisitor.h>
#include <Gdi/WinProc.h>
#include <Win32/DisplayMode.h>
#include <Win32/Thread.h>

namespace
{
	CRITICAL_SECTION* g_ddCs = nullptr;
	WNDPROC g_origDDrawWindowProc = nullptr;

	LRESULT handleActivateApp(HWND hwnd, WPARAM wParam, LPARAM lParam, WNDPROC origWndProc);
	LRESULT handleSize(HWND hwnd, WPARAM wParam, LPARAM lParam);

	template <typename TDirectDraw>
	HRESULT STDMETHODCALLTYPE CreatePalette(TDirectDraw* This, DWORD dwFlags, LPPALETTEENTRY lpDDColorArray,
		LPDIRECTDRAWPALETTE* lplpDDPalette, IUnknown* pUnkOuter)
	{
		if (lpDDColorArray)
		{
			DWORD count = 0;
			if (dwFlags & DDPCAPS_1BIT)
			{
				count = 2;
			}
			else if (dwFlags & DDPCAPS_2BIT)
			{
				count = 4;
			}
			else if (dwFlags & DDPCAPS_4BIT)
			{
				count = 16;
			}
			else if (dwFlags & DDPCAPS_8BIT)
			{
				count = 256;
			}
			LOG_DEBUG << Compat::array(lpDDColorArray, count);
		}
		return getOrigVtable(This).CreatePalette(This, dwFlags, lpDDColorArray, lplpDDPalette, pUnkOuter);
	}

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
		HRESULT result = DDERR_GENERIC;
		DDraw::LogUsedResourceFormat logUsedResourceFormat(desc2, reinterpret_cast<IUnknown*&>(*lplpDDSurface), result);

		if (lpDDSurfaceDesc->ddsCaps.dwCaps & DDSCAPS_PRIMARYSURFACE)
		{
			return result = DDraw::PrimarySurface::create<TDirectDraw>(*This, *lpDDSurfaceDesc, *lplpDDSurface);
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

			if (isPalettized && (desc.ddsCaps.dwCaps & DDSCAPS_TEXTURE) && !Config::palettizedTextures.get())
			{
				if (desc.ddsCaps.dwCaps & DDSCAPS_VIDEOMEMORY)
				{
					return result = DDERR_UNSUPPORTED;
				}
				desc.ddsCaps.dwCaps |= DDSCAPS_SYSTEMMEMORY;
			}
		}

		return result = DDraw::Surface::create<TDirectDraw>(*This, desc, *lplpDDSurface,
			std::make_unique<DDraw::Surface>(desc.dwFlags, lpDDSurfaceDesc->ddsCaps.dwCaps));
	}

	template <typename TDirectDraw>
	HRESULT STDMETHODCALLTYPE FlipToGDISurface(TDirectDraw* /*This*/)
	{
		return DDraw::PrimarySurface::flipToGdiSurface();
	}

	template <typename TDirectDraw>
	HRESULT STDMETHODCALLTYPE GetCaps(TDirectDraw* This, LPDDCAPS lpDDDriverCaps, LPDDCAPS lpDDHELCaps)
	{
		HRESULT result = getOrigVtable(This).GetCaps(This, lpDDDriverCaps, lpDDHELCaps);
		if (SUCCEEDED(result) && lpDDDriverCaps)
		{
			lpDDDriverCaps->dwZBufferBitDepths = DDraw::DirectDraw::getDevice(*This).getAdapter().getInfo().supportedZBufferBitDepths;

			DDCAPS caps = {};
			memcpy(&caps, lpDDDriverCaps, lpDDDriverCaps->dwSize);
			Config::capsPatches.applyPatches(caps);
			memcpy(lpDDDriverCaps, &caps, lpDDDriverCaps->dwSize);
		}
		return result;
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
					if (!isFullscreen)
					{
						DDraw::PrimarySurface::setWindowedCooperativeLevel();
					}
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

	void WINAPI ddrawEnterCriticalSection(LPCRITICAL_SECTION lpCriticalSection)
	{
		g_ddCs = lpCriticalSection;
		EnterCriticalSection(lpCriticalSection);
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

	CRITICAL_SECTION& getDDCS()
	{
		if (g_ddCs)
		{
			return *g_ddCs;
		}

		auto ddOrigEnterCriticalSection = Compat::hookIatFunction(
			Dll::g_origDDrawModule, "EnterCriticalSection", &ddrawEnterCriticalSection);
		Dll::g_origProcs.AcquireDDThreadLock();
		Compat::hookIatFunction(Dll::g_origDDrawModule, "EnterCriticalSection", ddOrigEnterCriticalSection);
		Dll::g_origProcs.ReleaseDDThreadLock();
		return *g_ddCs;
	}

	LRESULT handleActivateApp(HWND hwnd, WPARAM wParam, LPARAM lParam, WNDPROC origWndProc)
	{
		LOG_FUNC("DirectDraw::handleActivateApp", hwnd, wParam, lParam, origWndProc);

		if (Config::Settings::AltTabFix::OFF == Config::altTabFix.get())
		{
			return LOG_RESULT(g_origDDrawWindowProc(hwnd, WM_ACTIVATEAPP, wParam, lParam));
		}

		auto tagSurface = DDraw::TagSurface::findFullscreenWindow();
		const bool ignoreDdWndProc = Config::Settings::AltTabFix::NOACTIVATEAPP == Config::altTabFix.get() ||
			tagSurface && tagSurface->getExclusiveOwnerThreadId() != GetCurrentThreadId();
		const bool keepPrimary = ignoreDdWndProc ||
			Config::Settings::AltTabFix::KEEPVIDMEM == Config::altTabFix.get() && Config::altTabFix.getParam();

		Compat::ScopedCriticalSection lock(getDDCS());
		std::set<DDRAWI_DDRAWSURFACE_LCL*> surfacesToRestore;
		DDraw::Surface::enumSurfaces([&](const DDraw::Surface& surface)
			{
				auto lcl = DDraw::DirectDrawSurface::getInt(*surface.getDDS()).lpLcl;
				if (!(lcl->dwFlags & DDRAWISURF_INVALID) &&
					(keepPrimary || !surface.isPrimary()))
				{
					lcl->dwFlags |= DDRAWISURF_INVALID;
					surfacesToRestore.insert(lcl);
				}
			});

		LRESULT result = 0;
		if (ignoreDdWndProc)
		{
			{
				Win32::DisplayMode::incDisplaySettingsUniqueness();
				DDraw::ScopedThreadLock invalidateRealPrimary;
			}

			if (!wParam)
			{
				ShowWindow(hwnd, SW_SHOWMINNOACTIVE);
				CALL_ORIG_FUNC(ClipCursor)(nullptr);
			}

			if (Config::Settings::AltTabFix::NOACTIVATEAPP != Config::altTabFix.get() ||
				1 == Config::altTabFix.getParam())
			{
				result = LOG_RESULT(CallWindowProcA(origWndProc, hwnd, WM_ACTIVATEAPP, wParam, lParam));
			}
		}
		else
		{
			result = g_origDDrawWindowProc(hwnd, WM_ACTIVATEAPP, wParam, lParam);
		}

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
				DDraw::RealPrimarySurface::restore();
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
		vtable.CreatePalette = &CreatePalette;
		vtable.CreateSurface = &CreateSurface;
		vtable.FlipToGDISurface = &FlipToGDISurface;
		vtable.GetCaps = &GetCaps;
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
		}

		void onCreate(GUID* guid, CompatRef<IDirectDraw7> dd)
		{
			DDraw::ScopedThreadLock lock;
			D3dDdi::Device::findDeviceByDd(dd)->initRepository(guid);
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
			CompatVtable<Vtable>::template hookVtable<ScopedThreadLock>(vtable);
		}

		template void hookVtable(const IDirectDrawVtbl&);
		template void hookVtable(const IDirectDraw2Vtbl&);
		template void hookVtable(const IDirectDraw4Vtbl&);
		template void hookVtable(const IDirectDraw7Vtbl&);
	}
}
