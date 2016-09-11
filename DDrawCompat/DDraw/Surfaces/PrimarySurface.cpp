#include "Common/CompatPtr.h"
#include "DDraw/DisplayMode.h"
#include "DDraw/RealPrimarySurface.h"
#include "DDraw/Surfaces/PrimarySurface.h"
#include "DDraw/Surfaces/PrimarySurfaceImpl.h"

namespace
{
	DDSURFACEDESC2 g_primarySurfaceDesc = {};
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

		g_primarySurface = nullptr;
		s_palette = nullptr;
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

		CompatPtr<IDirectDraw7> dd7(Compat::queryInterface<IDirectDraw7>(&dd));
		const auto& dm = DisplayMode::getDisplayMode(*dd7);
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
		g_primarySurface = surface1;

		ZeroMemory(&g_primarySurfaceDesc, sizeof(g_primarySurfaceDesc));
		g_primarySurfaceDesc.dwSize = sizeof(g_primarySurfaceDesc);
		CompatVtableBase<IDirectDrawSurface7>::s_origVtable.GetSurfaceDesc(surface7, &g_primarySurfaceDesc);

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

	const DDSURFACEDESC2& PrimarySurface::getDesc()
	{
		return g_primarySurfaceDesc;
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

	CompatWeakPtr<IDirectDrawPalette> PrimarySurface::s_palette;
	PALETTEENTRY PrimarySurface::s_paletteEntries[256] = {};
}
