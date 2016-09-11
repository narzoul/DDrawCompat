#include "DDraw/Surfaces/SurfaceImpl.h"
#include "DDraw/Surfaces/FullScreenTagSurface.h"

namespace DDraw
{
	FullScreenTagSurface::FullScreenTagSurface(const std::function<void()>& releaseHandler)
		: m_releaseHandler(releaseHandler)
	{
	}

	FullScreenTagSurface::~FullScreenTagSurface()
	{
		m_releaseHandler();
	}

	HRESULT FullScreenTagSurface::create(CompatRef<IDirectDraw> dd, IDirectDrawSurface*& surface,
		const std::function<void()>& releaseHandler)
	{
		DDSURFACEDESC desc = {};
		desc.dwSize = sizeof(desc);
		desc.dwFlags = DDSD_WIDTH | DDSD_HEIGHT | DDSD_CAPS;
		desc.dwWidth = 1;
		desc.dwHeight = 1;
		desc.ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN | DDSCAPS_SYSTEMMEMORY;

		HRESULT result = Surface::create(dd, desc, surface);
		if (SUCCEEDED(result))
		{
			CompatPtr<IDirectDrawSurface7> surface7(Compat::queryInterface<IDirectDrawSurface7>(surface));
			std::unique_ptr<Surface> privateData(new FullScreenTagSurface(releaseHandler));
			attach(*surface7, privateData);
		}
		return result;
	}
}
