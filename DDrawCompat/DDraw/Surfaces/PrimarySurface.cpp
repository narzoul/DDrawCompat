#include "Common/CompatPtr.h"
#include "Config/Config.h"
#include "DDraw/DirectDraw.h"
#include "DDraw/RealPrimarySurface.h"
#include "DDraw/Surfaces/PrimarySurface.h"
#include "DDraw/Surfaces/PrimarySurfaceImpl.h"

namespace
{
	DDSURFACEDESC2 g_primarySurfaceDesc = {};
	CompatWeakPtr<IDirectDrawSurface> g_gdiSurface = nullptr;
	CompatWeakPtr<IDirectDrawSurface> g_primarySurface = nullptr;
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

		g_gdiSurface = nullptr;
		g_primarySurface = nullptr;
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

		CompatPtr<IDirectDrawSurface> surface1(Compat::queryInterface<IDirectDrawSurface>(surface));
		g_gdiSurface = surface1;
		g_primarySurface = surface1;

		ZeroMemory(&g_primarySurfaceDesc, sizeof(g_primarySurfaceDesc));
		g_primarySurfaceDesc.dwSize = sizeof(g_primarySurfaceDesc);
		CompatVtable<IDirectDrawSurface7Vtbl>::s_origVtable.GetSurfaceDesc(surface7, &g_primarySurfaceDesc);

		if (g_primarySurfaceDesc.ddsCaps.dwCaps & DDSCAPS_SYSTEMMEMORY)
		{
			resizeBuffers(*surface7);
		}

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
		if (!g_primarySurface)
		{
			return DDERR_NOTFOUND;
		}
		return g_primarySurface.get()->lpVtbl->Flip(g_primarySurface, g_gdiSurface, DDFLIP_WAIT);
	}

	const DDSURFACEDESC2& PrimarySurface::getDesc()
	{
		return g_primarySurfaceDesc;
	}

	CompatPtr<IDirectDrawSurface7> PrimarySurface::getGdiSurface()
	{
		return CompatPtr<IDirectDrawSurface7>::from(g_gdiSurface.get());
	}

	CompatPtr<IDirectDrawSurface7> PrimarySurface::getPrimary()
	{
		if (!g_primarySurface)
		{
			return nullptr;
		}
		return CompatPtr<IDirectDrawSurface7>(
			Compat::queryInterface<IDirectDrawSurface7>(g_primarySurface.get()));
	}

	void PrimarySurface::resizeBuffers(CompatRef<IDirectDrawSurface7> surface)
	{
		DDSCAPS2 flipCaps = {};
		flipCaps.dwCaps = DDSCAPS_FLIP;

		DDSURFACEDESC2 desc = {};
		desc.dwSize = sizeof(desc);
		desc.dwFlags = DDSD_LPSURFACE;

		const DWORD newBufferSize = g_primarySurfaceDesc.lPitch *
			(g_primarySurfaceDesc.dwHeight + Config::primarySurfaceExtraRows);

		auto surfacePtr(CompatPtr<IDirectDrawSurface7>::from(&surface));
		do
		{
			s_surfaceBuffers.push_back(std::vector<unsigned char>(newBufferSize));
			desc.lpSurface = s_surfaceBuffers.back().data();
			surfacePtr->SetSurfaceDesc(surfacePtr, &desc, 0);

			CompatPtr<IDirectDrawSurface7> nextSurface;
			surfacePtr->GetAttachedSurface(surfacePtr, &flipCaps, &nextSurface.getRef());
			surfacePtr.swap(nextSurface);
		} while (surfacePtr && surfacePtr != &surface);
	}

	void PrimarySurface::updateGdiSurfacePtr(IDirectDrawSurface* flipTargetOverride)
	{
		auto primary(CompatPtr<IDirectDrawSurface>::from(m_surface->getDirectDrawSurface().get()));
		if (flipTargetOverride)
		{
			if (g_gdiSurface.get() == flipTargetOverride)
			{
				g_gdiSurface = primary;
			}
			else if (g_gdiSurface.get() == primary)
			{
				g_gdiSurface = flipTargetOverride;
			}
			return;
		}

		DDSCAPS caps = {};
		caps.dwCaps = DDSCAPS_FLIP;
		CompatPtr<IDirectDrawSurface> current(primary);
		CompatPtr<IDirectDrawSurface> next;
		HRESULT result = current->GetAttachedSurface(current, &caps, &next.getRef());
		while (SUCCEEDED(result) && next.get() != g_gdiSurface.get() && next.get() != primary)
		{
			current = next;
			next.reset();
			result = current->GetAttachedSurface(current, &caps, &next.getRef());
		}

		g_gdiSurface = current;
	}

	CompatWeakPtr<IDirectDrawPalette> PrimarySurface::s_palette;
	PALETTEENTRY PrimarySurface::s_paletteEntries[256] = {};
	std::vector<std::vector<unsigned char>> PrimarySurface::s_surfaceBuffers;
}
