#include <Common/CompatPtr.h>
#include <Common/CompatRef.h>
#include <D3dDdi/Device.h>
#include <D3dDdi/KernelModeThunks.h>
#include <D3dDdi/Resource.h>
#include <D3dDdi/ScopedCriticalSection.h>
#include <D3dDdi/SurfaceRepository.h>
#include <DDraw/DirectDraw.h>
#include <DDraw/DirectDrawSurface.h>
#include <DDraw/RealPrimarySurface.h>
#include <DDraw/Surfaces/PrimarySurface.h>
#include <DDraw/Surfaces/PrimarySurfaceImpl.h>
#include <DDraw/Surfaces/TagSurface.h>
#include <Gdi/Palette.h>
#include <Gdi/VirtualScreen.h>
#include <Win32/DisplayMode.h>

namespace
{
	CompatWeakPtr<IDirectDrawSurface7> g_primarySurface;
	D3dDdi::Device* g_device = nullptr;
	HANDLE g_gdiDriverResource = nullptr;
	HANDLE g_gdiRuntimeResource = nullptr;
	D3dDdi::Resource* g_frontResource = nullptr;
	UINT g_frontResourceIndex = 0;
	DWORD g_origCaps = 0;
	HWND g_deviceWindow = nullptr;
	HPALETTE g_palette = nullptr;
	std::wstring g_deviceName;
	Win32::DisplayMode::MonitorInfo g_monitorInfo = {};
}

namespace DDraw
{
	PrimarySurface::~PrimarySurface()
	{
		LOG_FUNC("PrimarySurface::~PrimarySurface");

		g_device = nullptr;
		g_gdiRuntimeResource = nullptr;
		g_gdiDriverResource = nullptr;
		g_frontResource = nullptr;
		g_frontResourceIndex = 0;
		g_primarySurface = nullptr;
		g_origCaps = 0;
		g_deviceWindow = nullptr;
		if (g_palette)
		{
			DeleteObject(g_palette);
			g_palette = nullptr;
		}
		g_deviceName.clear();
		g_monitorInfo = {};
		s_palette = nullptr;

		DDraw::RealPrimarySurface::release();
		Gdi::VirtualScreen::setFullscreenMode(false);
	}

	template <typename TDirectDraw, typename TSurface, typename TSurfaceDesc>
	HRESULT PrimarySurface::create(CompatRef<TDirectDraw> dd, TSurfaceDesc desc, TSurface*& surface)
	{
		LOG_FUNC("PrimarySurface::create", &dd, desc, surface);

		auto deviceName = D3dDdi::KernelModeThunks::getAdapterInfo(*CompatPtr<IDirectDraw7>::from(&dd)).deviceName;
		auto prevMonitorInfo = g_monitorInfo;
		g_monitorInfo = Win32::DisplayMode::getMonitorInfo(deviceName);

		HRESULT result = RealPrimarySurface::create(*CompatPtr<IDirectDraw>::from(&dd));
		if (FAILED(result))
		{
			g_monitorInfo = prevMonitorInfo;
			return LOG_RESULT(result);
		}

		Gdi::VirtualScreen::setFullscreenMode(RealPrimarySurface::isFullscreen());

		const DWORD origCaps = desc.ddsCaps.dwCaps;
		auto privateData(std::make_unique<PrimarySurface>(desc.dwFlags, origCaps));
		auto data = privateData.get();

		desc.dwFlags |= DDSD_WIDTH | DDSD_HEIGHT | DDSD_PIXELFORMAT;
		desc.dwWidth = g_monitorInfo.rcEmulated.right - g_monitorInfo.rcEmulated.left;
		desc.dwHeight = g_monitorInfo.rcEmulated.bottom - g_monitorInfo.rcEmulated.top;
		desc.ddsCaps.dwCaps &= ~(DDSCAPS_PRIMARYSURFACE | DDSCAPS_SYSTEMMEMORY |
			DDSCAPS_VIDEOMEMORY | DDSCAPS_LOCALVIDMEM | DDSCAPS_NONLOCALVIDMEM);
		desc.ddsCaps.dwCaps |= DDSCAPS_OFFSCREENPLAIN;
		desc.ddpfPixelFormat = DirectDraw::getRgbPixelFormat(g_monitorInfo.bpp);

		if (!(desc.dwFlags & DDSD_BACKBUFFERCOUNT))
		{
			desc.ddsCaps.dwCaps |= DDSCAPS_3DDEVICE;
		}

		result = Surface::create(dd, desc, surface, std::move(privateData));
		if (FAILED(result))
		{
			LOG_ONCE("ERROR: Failed to create the compat primary surface: " << Compat::hex(result));
			RealPrimarySurface::release();
			g_monitorInfo = {};
			return LOG_RESULT(result);
		}

		if (g_primarySurface)
		{
			LOG_ONCE("WARNING: Multiple primary surfaces are not supported");
		}

		g_deviceName = deviceName;
		g_origCaps = origCaps;
		g_deviceWindow = *DDraw::DirectDraw::getDeviceWindowPtr(dd.get());

		if (desc.ddpfPixelFormat.dwRGBBitCount <= 8)
		{
			LOGPALETTE lp = {};
			lp.palVersion = 0x300;
			lp.palNumEntries = 1;
			g_palette = CreatePalette(&lp);
			ResizePalette(g_palette, 256);
		}

		g_device = D3dDdi::Device::findDeviceByResource(DirectDrawSurface::getDriverResourceHandle(*surface));
		data->restore();
		D3dDdi::Device::updateAllConfig();
		return LOG_RESULT(DD_OK);
	}

	template HRESULT PrimarySurface::create(
		CompatRef<IDirectDraw> dd, DDSURFACEDESC desc, IDirectDrawSurface*& surface);
	template HRESULT PrimarySurface::create(
		CompatRef<IDirectDraw2> dd, DDSURFACEDESC desc, IDirectDrawSurface*& surface);
	template HRESULT PrimarySurface::create(
		CompatRef<IDirectDraw4> dd, DDSURFACEDESC2 desc, IDirectDrawSurface4*& surface);
	template HRESULT PrimarySurface::create(
		CompatRef<IDirectDraw7> dd, DDSURFACEDESC2 desc, IDirectDrawSurface7*& surface);

	void PrimarySurface::createImpl()
	{
		m_impl.reset(new PrimarySurfaceImpl<IDirectDrawSurface>(this));
		m_impl2.reset(new PrimarySurfaceImpl<IDirectDrawSurface2>(this));
		m_impl3.reset(new PrimarySurfaceImpl<IDirectDrawSurface3>(this));
		m_impl4.reset(new PrimarySurfaceImpl<IDirectDrawSurface4>(this));
		m_impl7.reset(new PrimarySurfaceImpl<IDirectDrawSurface7>(this));
	}

	HRESULT PrimarySurface::flipToGdiSurface()
	{
		CompatPtr<IDirectDrawSurface7> gdiSurface;
		if (!g_primarySurface || !(gdiSurface = getGdiSurface()))
		{
			return DDERR_NOTFOUND;
		}
		if (gdiSurface == g_primarySurface)
		{
			return DD_OK;
		}
		return g_primarySurface.get()->lpVtbl->Flip(g_primarySurface, gdiSurface, DDFLIP_WAIT);
	}

	CompatPtr<IDirectDrawSurface7> PrimarySurface::getGdiSurface()
	{
		if (!g_primarySurface)
		{
			return nullptr;
		}

		DDSCAPS2 caps = {};
		caps.dwCaps = DDSCAPS_FLIP;
		CompatWeakPtr<IDirectDrawSurface7> surface(g_primarySurface);

		do
		{
			if (isGdiSurface(surface.get()))
			{
				return CompatPtr<IDirectDrawSurface7>::from(surface.get());
			}

			if (FAILED(surface->GetAttachedSurface(surface, &caps, &surface.getRef())))
			{
				return nullptr;
			}
			surface->Release(surface);
		} while (surface != g_primarySurface);

		return nullptr;
	}

	CompatPtr<IDirectDrawSurface7> PrimarySurface::getBackBuffer()
	{
		DDSCAPS2 caps = {};
		caps.dwCaps = DDSCAPS_BACKBUFFER;
		CompatPtr<IDirectDrawSurface7> backBuffer;
		g_primarySurface->GetAttachedSurface(g_primarySurface, &caps, &backBuffer.getRef());
		return backBuffer;
	}

	CompatPtr<IDirectDrawSurface7> PrimarySurface::getLastSurface()
	{
		DDSCAPS2 caps = {};
		caps.dwCaps = DDSCAPS_FLIP;
		auto surface(CompatPtr<IDirectDrawSurface7>::from(g_primarySurface.get()));
		CompatPtr<IDirectDrawSurface7> nextSurface;

		while (SUCCEEDED(surface->GetAttachedSurface(surface, &caps, &nextSurface.getRef())) &&
			nextSurface != g_primarySurface)
		{
			surface.swap(nextSurface);
			nextSurface.release();
		}

		return surface;
	}

	const Win32::DisplayMode::MonitorInfo& PrimarySurface::getMonitorInfo()
	{
		return g_monitorInfo;
	}

	CompatWeakPtr<IDirectDrawSurface7> PrimarySurface::getPrimary()
	{
		return g_primarySurface;
	}

	HANDLE PrimarySurface::getFrontResource()
	{
		if (!g_frontResource)
		{
			return nullptr;
		}
		return *g_frontResource;
	}

	HANDLE PrimarySurface::getGdiResource()
	{
		return g_gdiDriverResource;
	}
	
	DWORD PrimarySurface::getOrigCaps()
	{
		return g_origCaps;
	}

	template <typename TSurface>
	static bool PrimarySurface::isGdiSurface(TSurface* surface)
	{
		return surface && DirectDrawSurface::getRuntimeResourceHandle(*surface) == g_gdiRuntimeResource;
	}

	template bool PrimarySurface::isGdiSurface(IDirectDrawSurface*);
	template bool PrimarySurface::isGdiSurface(IDirectDrawSurface2*);
	template bool PrimarySurface::isGdiSurface(IDirectDrawSurface3*);
	template bool PrimarySurface::isGdiSurface(IDirectDrawSurface4*);
	template bool PrimarySurface::isGdiSurface(IDirectDrawSurface7*);

	void PrimarySurface::onLost()
	{
		if (s_palette)
		{
			Gdi::Palette::setHardwarePalette(Gdi::Palette::getSystemPalette().data());
		}
		g_gdiRuntimeResource = nullptr;
		g_gdiDriverResource = nullptr;
		Gdi::VirtualScreen::setFullscreenMode(false);
	}

	void PrimarySurface::restore()
	{
		LOG_FUNC("PrimarySurface::restore");

		updatePalette();
		Gdi::VirtualScreen::setFullscreenMode(RealPrimarySurface::isFullscreen());
		g_primarySurface = m_surface;
		g_gdiRuntimeResource = DirectDrawSurface::getRuntimeResourceHandle(*g_primarySurface);

		updateFrontResource();
		g_gdiDriverResource = *g_frontResource;
		D3dDdi::Device::setGdiResourceHandle(g_gdiDriverResource);

		DDSCAPS2 caps = {};
		caps.dwCaps = DDSCAPS_FLIP;
		auto surface(CompatPtr<IDirectDrawSurface7>::from(m_surface.get()));
		HRESULT result = S_OK;
		do
		{
			auto surf = DDraw::Surface::getSurface(*surface);
			if (surf)
			{
				surf->setAsPrimary();
			}

			CompatPtr<IDirectDrawSurface7> next;
			result = surface->GetAttachedSurface(surface, &caps, &next.getRef());
			next.swap(surface);
		} while (SUCCEEDED(result) && surface != m_surface);

		Surface::restore();
	}

	void PrimarySurface::setAsRenderTarget()
	{
		g_origCaps |= DDSCAPS_3DDEVICE;
		D3dDdi::ScopedCriticalSection lock;
		auto resource = D3dDdi::Device::findResource(DDraw::DirectDrawSurface::getDriverResourceHandle(*g_primarySurface));
		if (resource)
		{
			resource->setAsRenderTarget();
		}
	}

	void PrimarySurface::setWindowedCooperativeLevel()
	{
		LOG_FUNC("PrimarySurface::setWindowedCooperativeLevel");
		if (!g_primarySurface)
		{
			return;
		}

		RealPrimarySurface::restore();

		if (FAILED(g_primarySurface->IsLost(g_primarySurface)))
		{
			return;
		}

		auto surface = Surface::getSurface(*g_primarySurface);
		if (surface)
		{
			onLost();
			surface->restore();
		}
	}

	void PrimarySurface::updateFrontResource()
	{
		g_frontResource = D3dDdi::Device::findResource(DirectDrawSurface::getDriverResourceHandle(*g_primarySurface));
		g_frontResourceIndex = DirectDrawSurface::getSubResourceIndex(*g_primarySurface);
	}

	void PrimarySurface::updatePalette()
	{
		if (!s_palette)
		{
			if (DDraw::RealPrimarySurface::isFullscreen())
			{
				Gdi::Palette::setHardwarePalette(Gdi::Palette::getSystemPalette().data());
			}
			return;
		}

		PALETTEENTRY entries[256] = {};
		PrimarySurface::s_palette->GetEntries(s_palette, 0, 0, 256, entries);

		if (RealPrimarySurface::isFullscreen() && SUCCEEDED(g_primarySurface->IsLost(g_primarySurface)))
		{
			Gdi::Palette::setHardwarePalette(entries);
		}
		else
		{
			SetPaletteEntries(g_palette, 0, 256, entries);
			HDC dc = GetDC(g_deviceWindow);
			HPALETTE oldPal = SelectPalette(dc, g_palette, FALSE);
			RealizePalette(dc);
			SelectPalette(dc, oldPal, FALSE);
			ReleaseDC(g_deviceWindow, dc);
		}

		RealPrimarySurface::scheduleUpdate(true);
	}

	void PrimarySurface::waitForIdle()
	{
		if (g_device)
		{
			g_device->waitForIdle();
		}
	}

	CompatWeakPtr<IDirectDrawPalette> PrimarySurface::s_palette;
}
