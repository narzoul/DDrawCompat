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
	using CompatGdiDcCache::CompatDc;
	
	std::map<SurfaceMemoryDesc, std::vector<CompatDc>> g_compatDcCaches;
	std::vector<CompatDc>* g_currentCompatDcCache = nullptr;

	DWORD g_cacheId = 0;
	IDirectDraw7* g_directDraw = nullptr;
	void* g_surfaceMemory = nullptr;
	LONG g_pitch = 0;

	IDirectDrawSurface7* createGdiSurface();
	void releaseCompatDc(CompatDc compatDc);
	void releaseCompatDcCache(std::vector<CompatDc>& compatDcCache);

	void clearAllCaches()
	{
		for (auto& compatDcCache : g_compatDcCaches)
		{
			releaseCompatDcCache(compatDcCache.second);
		}
		g_compatDcCaches.clear();
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

	CompatDc createCompatDc()
	{
		CompatDc compatDc = {};

		IDirectDrawSurface7* surface = createGdiSurface();
		if (!surface)
		{
			return compatDc;
		}

		HDC dc = nullptr;
		HRESULT result = surface->lpVtbl->GetDC(surface, &dc);
		if (FAILED(result))
		{
			LOG_ONCE("Failed to create a GDI DC: " << result);
			surface->lpVtbl->Release(surface);
			return compatDc;
		}

		// Release DD critical section acquired by IDirectDrawSurface7::GetDC to avoid deadlocks
		Compat::origProcs.ReleaseDDThreadLock();

		compatDc.cacheId = g_cacheId;
		compatDc.surfaceMemoryDesc.surfaceMemory = g_surfaceMemory;
		compatDc.surfaceMemoryDesc.pitch = g_pitch;
		compatDc.dc = dc;
		compatDc.surface = surface;
		return compatDc;
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
			CompatDc compatDc = createCompatDc();
			if (!compatDc.dc)
			{
				return;
			}
			g_currentCompatDcCache->push_back(compatDc);
		}
	}

	void releaseCompatDc(CompatDc compatDc)
	{
		// Reacquire DD critical section that was temporarily released after IDirectDrawSurface7::GetDC
		Compat::origProcs.AcquireDDThreadLock();

		if (FAILED(CompatDirectDrawSurface<IDirectDrawSurface7>::s_origVtable.ReleaseDC(
			compatDc.surface, compatDc.dc)))
		{
			LOG_ONCE("Failed to release a cached DC");
			Compat::origProcs.ReleaseDDThreadLock();
		}

		CompatDirectDrawSurface<IDirectDrawSurface7>::s_origVtable.Release(compatDc.surface);
	}

	void releaseCompatDcCache(std::vector<CompatDc>& compatDcCache)
	{
		for (auto& compatDc : compatDcCache)
		{
			releaseCompatDc(compatDc);
		}
	}
}

namespace CompatGdiDcCache
{
	CompatDc getDc()
	{
		CompatDc compatDc = {};
		if (!g_currentCompatDcCache)
		{
			return compatDc;
		}

		if (g_currentCompatDcCache->empty())
		{
			LOG_ONCE("Warning: GDI DC cache size is insufficient");
			compatDc = createCompatDc();
			if (!compatDc.dc)
			{
				return compatDc;
			}
		}
		else
		{
			compatDc = g_currentCompatDcCache->back();
			g_currentCompatDcCache->pop_back();
		}

		compatDc.dcState = SaveDC(compatDc.dc);
		return compatDc;
	}

	bool init()
	{
		g_directDraw = createDirectDraw();
		return nullptr != g_directDraw;
	}

	bool isReleased()
	{
		return g_compatDcCaches.empty();
	}

	void release()
	{
		if (g_currentCompatDcCache)
		{
			g_currentCompatDcCache = nullptr;
			clearAllCaches();
			++g_cacheId;
		}
	}

	void returnDc(const CompatDc& compatDc)
	{
		RestoreDC(compatDc.dc, compatDc.dcState);
		
		if (compatDc.cacheId != g_cacheId)
		{
			releaseCompatDc(compatDc);
		}
		else
		{
			g_compatDcCaches[compatDc.surfaceMemoryDesc].push_back(compatDc);
		}
	}

	void setSurfaceMemory(void* surfaceMemory, LONG pitch)
	{
		g_surfaceMemory = surfaceMemory;
		g_pitch = pitch;

		if (!surfaceMemory)
		{
			g_currentCompatDcCache = nullptr;
			return;
		}

		SurfaceMemoryDesc surfaceMemoryDesc = { surfaceMemory, pitch };
		auto it = g_compatDcCaches.find(surfaceMemoryDesc);
		if (it == g_compatDcCaches.end())
		{
			g_currentCompatDcCache = &g_compatDcCaches[surfaceMemoryDesc];
			fillCurrentCache();
		}
		else
		{
			g_currentCompatDcCache = &it->second;
		}
	}
}
