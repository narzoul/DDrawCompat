#include "DDraw/Surfaces/SurfaceImpl.h"
#include "DDraw/Surfaces/FullScreenTagSurface.h"

namespace
{
	CompatWeakPtr<IDirectDrawSurface> g_surface = nullptr;
}

namespace DDraw
{
	FullScreenTagSurface::~FullScreenTagSurface()
	{
		g_surface = nullptr;
	}

	HRESULT FullScreenTagSurface::create(CompatRef<IDirectDraw> dd)
	{
		destroy();

		DDSURFACEDESC desc = {};
		desc.dwSize = sizeof(desc);
		desc.dwFlags = DDSD_WIDTH | DDSD_HEIGHT | DDSD_CAPS;
		desc.dwWidth = 1;
		desc.dwHeight = 1;
		desc.ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN | DDSCAPS_SYSTEMMEMORY;

		IDirectDrawSurface* surface = nullptr;
		HRESULT result = Surface::create(dd, desc, surface);
		if (SUCCEEDED(result))
		{
			CompatPtr<IDirectDrawSurface7> surface7(Compat::queryInterface<IDirectDrawSurface7>(surface));
			std::unique_ptr<Surface> privateData(new FullScreenTagSurface());
			attach(*surface7, privateData);
			g_surface = surface;
		}
		return result;
	}

	void FullScreenTagSurface::destroy()
	{
		g_surface.release();
	}

	CompatPtr<IDirectDraw7> FullScreenTagSurface::getFullScreenDirectDraw()
	{
		if (!g_surface)
		{
			return nullptr;
		}

		CompatPtr<IUnknown> dd = nullptr;
		auto tagSurface(getFullScreenTagSurface());
		tagSurface.get()->lpVtbl->GetDDInterface(tagSurface, reinterpret_cast<void**>(&dd.getRef()));
		return CompatPtr<IDirectDraw7>(Compat::queryInterface<IDirectDraw7>(dd.get()));
	}

	CompatPtr<IDirectDrawSurface7> FullScreenTagSurface::getFullScreenTagSurface()
	{
		return CompatPtr<IDirectDrawSurface7>(
			Compat::queryInterface<IDirectDrawSurface7>(g_surface.get()));
	}
}
