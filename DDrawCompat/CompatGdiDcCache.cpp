#include <map>
#include <vector>

#include "CompatDirectDraw.h"
#include "CompatDirectDrawSurface.h"
#include "CompatGdiDcCache.h"
#include "CompatPrimarySurface.h"
#include "Config.h"
#include "DDrawLog.h"
#include "DDrawProcs.h"
#include "DDrawScopedThreadLock.h"

namespace CompatGdiDcCache
{
	bool operator<(const SurfaceMemoryDesc& desc1, const SurfaceMemoryDesc& desc2)
	{
		return desc1.surfaceMemory < desc2.surfaceMemory ||
			(desc1.surfaceMemory == desc2.surfaceMemory && desc1.pitch < desc2.pitch);
	}
}

namespace
{
	using CompatGdiDcCache::SurfaceMemoryDesc;
	using CompatGdiDcCache::CachedDc;
	
	std::map<SurfaceMemoryDesc, std::vector<CachedDc>> g_caches;
	std::vector<CachedDc>* g_currentCache = nullptr;

	IDirectDraw7* g_directDraw = nullptr;
	void* g_surfaceMemory = nullptr;
	LONG g_pitch = 0;

	IDirectDrawSurface7* createGdiSurface();
	void releaseCachedDc(CachedDc cachedDc);
	void releaseCache(std::vector<CachedDc>& cache);

	void clearAllCaches()
	{
		for (auto& cache : g_caches)
		{
			releaseCache(cache.second);
		}
		g_caches.clear();
	}

	IDirectDraw7* createDirectDraw()
	{
		IDirectDraw7* dd = nullptr;
		CALL_ORIG_DDRAW(DirectDrawCreateEx, nullptr, reinterpret_cast<LPVOID*>(&dd), IID_IDirectDraw7, nullptr);
		if (!dd)
		{
			Compat::Log() << "Failed to create a DirectDraw interface for GDI";
			return nullptr;
		}

		if (FAILED(CompatDirectDraw<IDirectDraw7>::s_origVtable.SetCooperativeLevel(dd, nullptr, DDSCL_NORMAL)))
		{
			Compat::Log() << "Failed to set the cooperative level on the DirectDraw interface for GDI";
			dd->lpVtbl->Release(dd);
			return nullptr;
		}

		return dd;
	}

	CachedDc createCachedDc()
	{
		CachedDc cachedDc = {};

		IDirectDrawSurface7* surface = createGdiSurface();
		if (!surface)
		{
			return cachedDc;
		}

		HDC dc = nullptr;
		HRESULT result = surface->lpVtbl->GetDC(surface, &dc);
		if (FAILED(result))
		{
			LOG_ONCE("Failed to create a GDI DC: " << result);
			surface->lpVtbl->Release(surface);
			return cachedDc;
		}

		// Release DD critical section acquired by IDirectDrawSurface7::GetDC to avoid deadlocks
		Compat::origProcs.ReleaseDDThreadLock();

		cachedDc.surfaceMemoryDesc.surfaceMemory = g_surfaceMemory;
		cachedDc.surfaceMemoryDesc.pitch = g_pitch;
		cachedDc.dc = dc;
		cachedDc.surface = surface;
		return cachedDc;
	}

	IDirectDrawSurface7* createGdiSurface()
	{
		Compat::DDrawScopedThreadLock ddLock;

		DDSURFACEDESC2 desc = {};
		desc.dwSize = sizeof(desc);
		desc.dwFlags = DDSD_WIDTH | DDSD_HEIGHT | DDSD_PIXELFORMAT | DDSD_CAPS | DDSD_PITCH | DDSD_LPSURFACE;
		desc.dwWidth = CompatPrimarySurface::width;
		desc.dwHeight = CompatPrimarySurface::height;
		desc.ddpfPixelFormat = CompatPrimarySurface::pixelFormat;
		desc.ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN | DDSCAPS_SYSTEMMEMORY;
		desc.lPitch = g_pitch;
		desc.lpSurface = g_surfaceMemory;

		IDirectDrawSurface7* surface = nullptr;
		HRESULT result = CompatDirectDraw<IDirectDraw7>::s_origVtable.CreateSurface(
			g_directDraw, &desc, &surface, nullptr);
		if (FAILED(result))
		{
			LOG_ONCE("Failed to create a GDI surface: " << result);
			return nullptr;
		}

		if (CompatPrimarySurface::palette)
		{
			CompatDirectDrawSurface<IDirectDrawSurface7>::s_origVtable.SetPalette(
				surface, CompatPrimarySurface::palette);
		}

		return surface;
	}

	void fillCurrentCache()
	{
		for (DWORD i = 0; i < Config::gdiDcCacheSize; ++i)
		{
			CachedDc cachedDc = createCachedDc();
			if (!cachedDc.dc)
			{
				return;
			}
			g_currentCache->push_back(cachedDc);
		}
	}

	void releaseCachedDc(CachedDc cachedDc)
	{
		// Reacquire DD critical section that was temporarily released after IDirectDrawSurface7::GetDC
		Compat::origProcs.AcquireDDThreadLock();

		if (FAILED(CompatDirectDrawSurface<IDirectDrawSurface7>::s_origVtable.ReleaseDC(
			cachedDc.surface, cachedDc.dc)))
		{
			LOG_ONCE("Failed to release a cached DC");
			Compat::origProcs.ReleaseDDThreadLock();
		}

		CompatDirectDrawSurface<IDirectDrawSurface7>::s_origVtable.Release(cachedDc.surface);
	}

	void releaseCache(std::vector<CachedDc>& cache)
	{
		for (auto& cachedDc : cache)
		{
			releaseCachedDc(cachedDc);
		}
	}
}

namespace CompatGdiDcCache
{
	CachedDc getDc()
	{
		CachedDc cachedDc = {};
		if (!g_currentCache)
		{
			return cachedDc;
		}

		if (g_currentCache->empty())
		{
			LOG_ONCE("Warning: GDI DC cache size is insufficient");
			cachedDc = createCachedDc();
			if (!cachedDc.dc)
			{
				return cachedDc;
			}
		}
		else
		{
			cachedDc = g_currentCache->back();
			g_currentCache->pop_back();
		}

		return cachedDc;
	}

	bool init()
	{
		g_directDraw = createDirectDraw();
		return nullptr != g_directDraw;
	}

	bool isReleased()
	{
		return g_caches.empty();
	}

	void release()
	{
		if (g_currentCache)
		{
			g_currentCache = nullptr;
			clearAllCaches();
		}
	}

	void releaseDc(const CachedDc& cachedDc)
	{
		g_caches[cachedDc.surfaceMemoryDesc].push_back(cachedDc);
	}

	void setSurfaceMemory(void* surfaceMemory, LONG pitch)
	{
		g_surfaceMemory = surfaceMemory;
		g_pitch = pitch;

		if (!surfaceMemory)
		{
			g_currentCache = nullptr;
			return;
		}

		SurfaceMemoryDesc surfaceMemoryDesc = { surfaceMemory, pitch };
		auto it = g_caches.find(surfaceMemoryDesc);
		if (it == g_caches.end())
		{
			g_currentCache = &g_caches[surfaceMemoryDesc];
			fillCurrentCache();
		}
		else
		{
			g_currentCache = &it->second;
		}
	}
}
