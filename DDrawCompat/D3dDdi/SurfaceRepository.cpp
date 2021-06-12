#include <map>

#include <Common/Comparison.h>
#include <D3dDdi/Adapter.h>
#include <D3dDdi/Device.h>
#include <D3dDdi/SurfaceRepository.h>
#include <DDraw/DirectDrawSurface.h>

namespace
{
	std::map<LUID, D3dDdi::SurfaceRepository> g_repositories;
}

namespace D3dDdi
{
	SurfaceRepository::SurfaceRepository(const Adapter& adapter)
		: m_adapter(adapter)
	{
	}

	CompatWeakPtr<IDirectDrawSurface7> SurfaceRepository::createSurface(
		DWORD width, DWORD height, const DDPIXELFORMAT& pf, DWORD caps)
	{
		auto dd(m_adapter.getRepository());
		if (!dd)
		{
			LOG_ONCE("ERROR: no DirectDraw repository available");
			return nullptr;
		}

		CompatPtr<IDirectDrawSurface7> surface;

		DDSURFACEDESC2 desc = {};
		desc.dwSize = sizeof(desc);
		desc.dwFlags = DDSD_WIDTH | DDSD_HEIGHT | DDSD_PIXELFORMAT | DDSD_CAPS;
		desc.dwWidth = width;
		desc.dwHeight = height;
		desc.ddpfPixelFormat = pf;
		desc.ddsCaps.dwCaps = caps;

		HRESULT result = dd->CreateSurface(dd, &desc, &surface.getRef(), nullptr);
		if (FAILED(result))
		{
			LOG_ONCE("ERROR: Failed to create repository surface: " << Compat::hex(result) << " " << desc);
			return nullptr;
		}
		return surface.detach();
	}

	SurfaceRepository& SurfaceRepository::get(const Adapter& adapter)
	{
		auto it = g_repositories.find(adapter.getLuid());
		if (it != g_repositories.end())
		{
			return it->second;
		}
		return g_repositories.emplace(adapter.getLuid(), SurfaceRepository(adapter)).first->second;
	}

	Resource* SurfaceRepository::getPaletteBltRenderTarget(DWORD width, DWORD height)
	{
		return getResource(m_paletteBltRenderTarget, width, height, DDraw::DirectDraw::getRgbPixelFormat(32),
			DDSCAPS_3DDEVICE | DDSCAPS_TEXTURE | DDSCAPS_VIDEOMEMORY);
	}

	Resource* SurfaceRepository::getPaletteTexture()
	{
		return getResource(m_paletteTexture, 256, 1, DDraw::DirectDraw::getRgbPixelFormat(32),
			DDSCAPS_TEXTURE | DDSCAPS_VIDEOMEMORY);
	}

	Resource* SurfaceRepository::getResource(Surface& surface, DWORD width, DWORD height, const DDPIXELFORMAT& pf, DWORD caps)
	{
		if (surface.surface)
		{
			DDSURFACEDESC2 desc = {};
			desc.dwSize = sizeof(desc);
			surface.surface->GetSurfaceDesc(surface.surface, &desc);
			if (desc.dwWidth != width || desc.dwHeight != height || FAILED(surface.surface->IsLost(surface.surface)))
			{
				surface.surface->Release(surface.surface);
				surface = {};
			}
		}

		if (!surface.surface)
		{
			surface.surface = createSurface(width, height, pf, caps);
			if (surface.surface)
			{
				surface.resource = D3dDdi::Device::findResource(
					DDraw::DirectDrawSurface::getDriverResourceHandle(*surface.surface));
			}
		}

		return surface.resource;
	}
}
