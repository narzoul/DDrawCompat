#include <algorithm>
#include <vector>

#include "Common/CompatPtr.h"
#include "Common/Log.h"
#include "DDraw/Repository.h"
#include "Dll/Procs.h"

namespace
{
	using DDraw::Repository::Surface;

	static std::vector<Surface> g_sysMemSurfaces;
	static std::vector<Surface> g_vidMemSurfaces;

	CompatPtr<IDirectDraw7> createDirectDraw();
	Surface createSurface(DWORD width, DWORD height, const DDPIXELFORMAT& pf, DWORD caps);
	std::vector<Surface>::iterator findSurface(DWORD width, DWORD height, const DDPIXELFORMAT& pf,
		std::vector<Surface>& cachedSurfaces);
	void destroySmallerSurfaces(DWORD width, DWORD height, const DDPIXELFORMAT& pf,
		std::vector<Surface>& cachedSurfaces);
	Surface getSurface(const DDSURFACEDESC2& desc);
	void normalizePixelFormat(DDPIXELFORMAT& pf);
	void returnSurface(const Surface& surface);

	CompatPtr<IDirectDraw7> createDirectDraw()
	{
		CompatPtr<IDirectDraw7> dd;
		HRESULT result = CALL_ORIG_PROC(DirectDrawCreateEx, nullptr,
			reinterpret_cast<void**>(&dd.getRef()), IID_IDirectDraw7, nullptr);
		if (FAILED(result))
		{
			Compat::Log() << "Failed to create a DirectDraw object in the repository: " << result;
			return nullptr;
		}

		result = dd->SetCooperativeLevel(dd, nullptr, DDSCL_NORMAL);
		if (FAILED(result))
		{
			Compat::Log() << "Failed to set the cooperative level in the repository: " << result;
			return nullptr;
		}

		return dd;
	}

	Surface createSurface(DWORD width, DWORD height, const DDPIXELFORMAT& pf, DWORD caps)
	{
		Surface surface = {};

		surface.desc.dwSize = sizeof(surface.desc);
		surface.desc.dwFlags = DDSD_WIDTH | DDSD_HEIGHT | DDSD_PIXELFORMAT | DDSD_CAPS;
		surface.desc.dwWidth = width;
		surface.desc.dwHeight = height;
		surface.desc.ddpfPixelFormat = pf;
		surface.desc.ddsCaps.dwCaps = caps;

		auto dd(DDraw::Repository::getDirectDraw());
		dd->CreateSurface(dd, &surface.desc, &surface.surface.getRef(), nullptr);
		return surface;
	}

	void destroySmallerSurfaces(DWORD width, DWORD height, const DDPIXELFORMAT& pf,
		std::vector<Surface>& cachedSurfaces)
	{
		auto it = cachedSurfaces.begin();
		while (it != cachedSurfaces.end())
		{
			if (it->desc.dwWidth <= width && it->desc.dwHeight <= height &&
				0 == memcmp(&it->desc.ddpfPixelFormat, &pf, sizeof(pf)))
			{
				it->surface.release();
				it = cachedSurfaces.erase(it);
			}
			else
			{
				++it;
			}
		}
	}

	std::vector<Surface>::iterator findSurface(DWORD width, DWORD height, const DDPIXELFORMAT& pf,
		std::vector<Surface>& cachedSurfaces)
	{
		auto it = cachedSurfaces.begin();
		while (it != cachedSurfaces.end())
		{
			if (it->desc.dwWidth >= width && it->desc.dwHeight >= height &&
				0 == memcmp(&it->desc.ddpfPixelFormat, &pf, sizeof(pf)))
			{
				if (FAILED(it->surface->IsLost(it->surface)) &&
					FAILED(it->surface->Restore(it->surface)))
				{
					it->surface.release();
					it = cachedSurfaces.erase(it);
					continue;
				}
				return it;
			}
			++it;
		}

		return cachedSurfaces.end();
	}

	Surface getSurface(const DDSURFACEDESC2& desc)
	{
		std::vector<Surface>& cachedSurfaces =
			(desc.ddsCaps.dwCaps & DDSCAPS_SYSTEMMEMORY) ? g_sysMemSurfaces : g_vidMemSurfaces;

		DDPIXELFORMAT pf = desc.ddpfPixelFormat;
		normalizePixelFormat(pf);

		auto it = findSurface(desc.dwWidth, desc.dwHeight, pf, cachedSurfaces);
		if (it != cachedSurfaces.end())
		{
			Surface cachedSurface = *it;
			cachedSurfaces.erase(it);
			return cachedSurface;
		}

		Surface newSurface = createSurface(desc.dwWidth, desc.dwHeight, pf,
			DDSCAPS_OFFSCREENPLAIN | (desc.ddsCaps.dwCaps & DDSCAPS_SYSTEMMEMORY));
		if (newSurface.surface)
		{
			destroySmallerSurfaces(desc.dwWidth, desc.dwHeight, pf, cachedSurfaces);
		}
		return newSurface;
	}

	void normalizePixelFormat(DDPIXELFORMAT& pf)
	{
		if (!(pf.dwFlags & DDPF_FOURCC))
		{
			pf.dwFourCC = 0;
		}
		if (!(pf.dwFlags & (DDPF_ALPHAPIXELS | DDPF_ZPIXELS)))
		{
			pf.dwRGBAlphaBitMask = 0;
		}
	}

	void returnSurface(const Surface& surface)
	{
		if (!surface.surface)
		{
			return;
		}

		if (surface.desc.ddsCaps.dwCaps & DDSCAPS_SYSTEMMEMORY)
		{
			g_sysMemSurfaces.push_back(surface);
		}
		else
		{
			g_vidMemSurfaces.push_back(surface);
		}
	}
}

namespace DDraw
{
	namespace Repository
	{
		ScopedSurface::ScopedSurface(const DDSURFACEDESC2& desc)
			: Surface(getSurface(desc))
		{
		}

		ScopedSurface::~ScopedSurface()
		{
			returnSurface(*this);
		}

		CompatWeakPtr<IDirectDraw7> getDirectDraw()
		{
			static auto dd = new CompatPtr<IDirectDraw7>(createDirectDraw());
			return *dd;
		}
	}
}
