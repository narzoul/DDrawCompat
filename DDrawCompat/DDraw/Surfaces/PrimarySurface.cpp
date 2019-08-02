#include "Common/CompatPtr.h"
#include "Common/CompatRef.h"
#include "Config/Config.h"
#include "D3dDdi/Device.h"
#include "D3dDdi/KernelModeThunks.h"
#include "DDraw/DirectDraw.h"
#include "DDraw/DirectDrawSurface.h"
#include "DDraw/RealPrimarySurface.h"
#include "DDraw/Surfaces/PrimarySurface.h"
#include "DDraw/Surfaces/PrimarySurfaceImpl.h"
#include "Gdi/Palette.h"
#include "Gdi/VirtualScreen.h"

namespace
{
	CompatWeakPtr<IDirectDrawSurface7> g_primarySurface;
	std::vector<CompatWeakPtr<IDirectDrawSurface7>> g_lockBackBuffers;
	HANDLE g_gdiResourceHandle = nullptr;
	HANDLE g_frontResource = nullptr;
	DWORD g_origCaps = 0;
}

namespace DDraw
{
	PrimarySurface::~PrimarySurface()
	{
		LOG_FUNC("PrimarySurface::~PrimarySurface");

		g_gdiResourceHandle = nullptr;
		g_frontResource = nullptr;
		g_primarySurface = nullptr;
		g_origCaps = 0;
		s_palette = nullptr;

		for (auto& lockBuffer : g_lockBackBuffers)
		{
			lockBuffer.release();
		}
		g_lockBackBuffers.clear();

		DDraw::RealPrimarySurface::release();
	}

	template <typename TDirectDraw, typename TSurface, typename TSurfaceDesc>
	HRESULT PrimarySurface::create(CompatRef<TDirectDraw> dd, TSurfaceDesc desc, TSurface*& surface)
	{
		HRESULT result = RealPrimarySurface::create(dd);
		if (FAILED(result))
		{
			return result;
		}

		const DWORD origCaps = desc.ddsCaps.dwCaps;

		const auto& dm = DDraw::getDisplayMode(*CompatPtr<IDirectDraw7>::from(&dd));
		desc.dwFlags |= DDSD_WIDTH | DDSD_HEIGHT | DDSD_PIXELFORMAT;
		desc.dwWidth = dm.dwWidth;
		desc.dwHeight = dm.dwHeight;
		desc.ddsCaps.dwCaps &= ~(DDSCAPS_PRIMARYSURFACE | DDSCAPS_SYSTEMMEMORY | DDSCAPS_NONLOCALVIDMEM);
		desc.ddsCaps.dwCaps |= DDSCAPS_OFFSCREENPLAIN | DDSCAPS_VIDEOMEMORY | DDSCAPS_LOCALVIDMEM;
		desc.ddpfPixelFormat = dm.ddpfPixelFormat;

		auto privateData(std::make_unique<PrimarySurface>());
		auto data = privateData.get();
		result = Surface::create(dd, desc, surface, std::move(privateData));
		if (FAILED(result))
		{
			Compat::Log() << "ERROR: Failed to create the compat primary surface: " << Compat::hex(result);
			RealPrimarySurface::release();
			return result;
		}

		g_origCaps = origCaps;

		data->m_lockSurface.release();
		data->m_attachedLockSurfaces.clear();

		desc.dwFlags = DDSD_WIDTH | DDSD_HEIGHT | DDSD_PIXELFORMAT | DDSD_CAPS;
		desc.ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN | DDSCAPS_SYSTEMMEMORY;
		CompatPtr<TSurface> lockSurface;
		dd->CreateSurface(&dd, &desc, &lockSurface.getRef(), nullptr);
		data->m_lockSurface = lockSurface;

		if (g_origCaps & DDSCAPS_FLIP)
		{
			for (std::size_t i = 0; i < desc.dwBackBufferCount; ++i)
			{
				CompatPtr<TSurface> lockBuffer;
				dd->CreateSurface(&dd, &desc, &lockBuffer.getRef(), nullptr);
				if (lockBuffer)
				{
					g_lockBackBuffers.push_back(CompatPtr<IDirectDrawSurface7>::from(lockBuffer.get()).detach());
					data->m_attachedLockSurfaces.push_back(g_lockBackBuffers.back());
				}
			}
		}

		data->restore();
		return DD_OK;
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
		m_impl.reset(new PrimarySurfaceImpl<IDirectDrawSurface>());
		m_impl2.reset(new PrimarySurfaceImpl<IDirectDrawSurface2>());
		m_impl3.reset(new PrimarySurfaceImpl<IDirectDrawSurface3>());
		m_impl4.reset(new PrimarySurfaceImpl<IDirectDrawSurface4>());
		m_impl7.reset(new PrimarySurfaceImpl<IDirectDrawSurface7>());
	}

	HRESULT PrimarySurface::flipToGdiSurface()
	{
		CompatPtr<IDirectDrawSurface7> gdiSurface;
		if (!g_primarySurface || !(gdiSurface = getGdiSurface()))
		{
			return DDERR_NOTFOUND;
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
			surface = nextSurface;
		}

		return surface;
	}

	CompatWeakPtr<IDirectDrawSurface7> PrimarySurface::getPrimary()
	{
		return g_primarySurface;
	}

	HANDLE PrimarySurface::getFrontResource()
	{
		return g_frontResource;
	}

	DWORD PrimarySurface::getOrigCaps()
	{
		return g_origCaps;
	}

	template <typename TSurface>
	static bool PrimarySurface::isGdiSurface(TSurface* surface)
	{
		return surface && getRuntimeResourceHandle(*surface) == g_gdiResourceHandle;
	}

	template bool PrimarySurface::isGdiSurface(IDirectDrawSurface*);
	template bool PrimarySurface::isGdiSurface(IDirectDrawSurface2*);
	template bool PrimarySurface::isGdiSurface(IDirectDrawSurface3*);
	template bool PrimarySurface::isGdiSurface(IDirectDrawSurface4*);
	template bool PrimarySurface::isGdiSurface(IDirectDrawSurface7*);

	void PrimarySurface::restore()
	{
		LOG_FUNC("PrimarySurface::restore");

		clearResources();

		Gdi::VirtualScreen::update();
		auto desc = Gdi::VirtualScreen::getSurfaceDesc(D3dDdi::KernelModeThunks::getMonitorRect());
		desc.dwFlags &= ~DDSD_CAPS;
		m_lockSurface->SetSurfaceDesc(m_lockSurface, &desc, 0);

		g_primarySurface = m_surface;
		g_gdiResourceHandle = getRuntimeResourceHandle(*g_primarySurface);
		updateFrontResource();
		D3dDdi::Device::setGdiResourceHandle(g_frontResource);

		Surface::restore();
	}

	void PrimarySurface::updateFrontResource()
	{
		g_frontResource = getDriverResourceHandle(*g_primarySurface);
	}

	void PrimarySurface::updatePalette()
	{
		PALETTEENTRY entries[256] = {};
		if (s_palette)
		{
			PrimarySurface::s_palette->GetEntries(s_palette, 0, 0, 256, entries);
		}

		if (RealPrimarySurface::isFullScreen())
		{
			if (!s_palette)
			{
				auto sysPalEntries(Gdi::Palette::getSystemPalette());
				std::memcpy(entries, sysPalEntries.data(), sizeof(entries));
			}
			Gdi::Palette::setHardwarePalette(entries);
		}
		else if (s_palette)
		{
			Gdi::Palette::setSystemPalette(entries, 256, false);
		}

		RealPrimarySurface::update();
	}

	CompatWeakPtr<IDirectDrawPalette> PrimarySurface::s_palette;
}
