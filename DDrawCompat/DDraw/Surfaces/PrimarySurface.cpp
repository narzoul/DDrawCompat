#include "Common/CompatPtr.h"
#include "Common/CompatRef.h"
#include "Config/Config.h"
#include "D3dDdi/Device.h"
#include "D3dDdi/KernelModeThunks.h"
#include "DDraw/DirectDraw.h"
#include "DDraw/RealPrimarySurface.h"
#include "DDraw/Surfaces/PrimarySurface.h"
#include "DDraw/Surfaces/PrimarySurfaceImpl.h"

namespace
{
	DDSURFACEDESC2 g_primarySurfaceDesc = {};
	CompatWeakPtr<IDirectDrawSurface7> g_primarySurface;
	HANDLE g_gdiResourceHandle = nullptr;
	DWORD g_origCaps = 0;

	template <typename TSurface>
	HANDLE getResourceHandle(TSurface& surface)
	{
		return reinterpret_cast<HANDLE**>(&surface)[1][2];
	}
}

namespace DDraw
{
	PrimarySurface::PrimarySurface(Surface* surface) : m_surface(surface)
	{
		surface->AddRef();
	}

	PrimarySurface::~PrimarySurface()
	{
		Compat::LogEnter("PrimarySurface::~PrimarySurface");

		g_gdiResourceHandle = nullptr;
		g_primarySurface = nullptr;
		g_origCaps = 0;
		s_palette = nullptr;
		s_surfaceBuffers.clear();
		ZeroMemory(&s_paletteEntries, sizeof(s_paletteEntries));
		ZeroMemory(&g_primarySurfaceDesc, sizeof(g_primarySurfaceDesc));

		DDraw::RealPrimarySurface::release();

		Compat::LogLeave("PrimarySurface::~PrimarySurface");
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
		desc.ddsCaps.dwCaps &= ~DDSCAPS_PRIMARYSURFACE;
		desc.ddsCaps.dwCaps |= DDSCAPS_OFFSCREENPLAIN;
		desc.ddpfPixelFormat = dm.ddpfPixelFormat;

		result = Surface::create(dd, desc, surface);
		if (FAILED(result))
		{
			Compat::Log() << "Failed to create the compat primary surface!";
			RealPrimarySurface::release();
			return result;
		}

		CompatPtr<IDirectDrawSurface7> surface7(Compat::queryInterface<IDirectDrawSurface7>(surface));
		std::unique_ptr<Surface> privateData(new PrimarySurface(Surface::getSurface(*surface)));
		attach(*surface7, privateData);

		g_primarySurface = surface7;
		g_origCaps = origCaps;

		onRestore();

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
		m_impl.reset(new PrimarySurfaceImpl<IDirectDrawSurface>(*m_surface->getImpl<IDirectDrawSurface>()));
		m_impl2.reset(new PrimarySurfaceImpl<IDirectDrawSurface2>(*m_surface->getImpl<IDirectDrawSurface2>()));
		m_impl3.reset(new PrimarySurfaceImpl<IDirectDrawSurface3>(*m_surface->getImpl<IDirectDrawSurface3>()));
		m_impl4.reset(new PrimarySurfaceImpl<IDirectDrawSurface4>(*m_surface->getImpl<IDirectDrawSurface4>()));
		m_impl7.reset(new PrimarySurfaceImpl<IDirectDrawSurface7>(*m_surface->getImpl<IDirectDrawSurface7>()));
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

	const DDSURFACEDESC2& PrimarySurface::getDesc()
	{
		return g_primarySurfaceDesc;
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

	DWORD PrimarySurface::getOrigCaps()
	{
		return g_origCaps;
	}

	template <typename TSurface>
	static bool PrimarySurface::isGdiSurface(TSurface* surface)
	{
		return surface && getResourceHandle(*surface) == g_gdiResourceHandle;
	}

	template bool PrimarySurface::isGdiSurface(IDirectDrawSurface*);
	template bool PrimarySurface::isGdiSurface(IDirectDrawSurface2*);
	template bool PrimarySurface::isGdiSurface(IDirectDrawSurface3*);
	template bool PrimarySurface::isGdiSurface(IDirectDrawSurface4*);
	template bool PrimarySurface::isGdiSurface(IDirectDrawSurface7*);

	void PrimarySurface::onRestore()
	{
		g_primarySurfaceDesc = {};
		g_primarySurfaceDesc.dwSize = sizeof(g_primarySurfaceDesc);
		g_primarySurface->GetSurfaceDesc(g_primarySurface, &g_primarySurfaceDesc);

		if (g_primarySurfaceDesc.ddsCaps.dwCaps & DDSCAPS_SYSTEMMEMORY)
		{
			resizeBuffers();
		}

		g_gdiResourceHandle = getResourceHandle(*g_primarySurface);
		D3dDdi::Device::setGdiResourceHandle(*reinterpret_cast<HANDLE*>(g_gdiResourceHandle));
	}

	void PrimarySurface::resizeBuffers()
	{
		DDSCAPS2 flipCaps = {};
		flipCaps.dwCaps = DDSCAPS_FLIP;

		DDSURFACEDESC2 desc = {};
		desc.dwSize = sizeof(desc);
		desc.dwFlags = DDSD_LPSURFACE;

		const DWORD newBufferSize = g_primarySurfaceDesc.lPitch *
			(g_primarySurfaceDesc.dwHeight + Config::primarySurfaceExtraRows);

		auto surfacePtr(CompatPtr<IDirectDrawSurface7>::from(g_primarySurface.get()));
		do
		{
			s_surfaceBuffers.push_back(std::vector<unsigned char>(newBufferSize));
			desc.lpSurface = s_surfaceBuffers.back().data();
			surfacePtr->SetSurfaceDesc(surfacePtr, &desc, 0);

			CompatPtr<IDirectDrawSurface7> nextSurface;
			surfacePtr->GetAttachedSurface(surfacePtr, &flipCaps, &nextSurface.getRef());
			surfacePtr.swap(nextSurface);
		} while (surfacePtr && surfacePtr != g_primarySurface.get());
	}

	CompatWeakPtr<IDirectDrawPalette> PrimarySurface::s_palette;
	PALETTEENTRY PrimarySurface::s_paletteEntries[256] = {};
	std::vector<std::vector<unsigned char>> PrimarySurface::s_surfaceBuffers;
}
