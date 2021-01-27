#include <map>
#include <memory>
#include <vector>

#include <Common/ScopedCriticalSection.h>
#include <Gdi/DcCache.h>
#include <Gdi/VirtualScreen.h>

namespace
{
	struct Cache
	{
		std::vector<std::unique_ptr<HDC__, void(*)(HDC)>> cache;
		std::vector<std::unique_ptr<HDC__, void(*)(HDC)>> defPalCache;
	};

	Compat::CriticalSection g_cs;
	std::map<DWORD, Cache> g_threadIdToDcCache;
}

namespace Gdi
{
	namespace DcCache
	{
		void dllProcessDetach()
		{
			Compat::ScopedCriticalSection lock(g_cs);
			g_threadIdToDcCache.clear();
		}

		void dllThreadDetach()
		{
			Compat::ScopedCriticalSection lock(g_cs);
			g_threadIdToDcCache.erase(GetCurrentThreadId());
		}

		HDC getDc(bool useDefaultPalette)
		{
			Compat::ScopedCriticalSection lock(g_cs);
			auto& cache = g_threadIdToDcCache[GetCurrentThreadId()];
			auto& dcCache = useDefaultPalette ? cache.defPalCache : cache.cache;

			if (dcCache.empty())
			{
				return Gdi::VirtualScreen::createDc(useDefaultPalette);
			}

			HDC dc = dcCache.back().release();
			dcCache.pop_back();
			return dc;
		}

		void releaseDc(HDC cachedDc, bool useDefaultPalette)
		{
			Compat::ScopedCriticalSection lock(g_cs);
			auto& cache = g_threadIdToDcCache[GetCurrentThreadId()];
			auto& dcCache = useDefaultPalette ? cache.defPalCache : cache.cache;
			dcCache.emplace_back(cachedDc, Gdi::VirtualScreen::deleteDc);
		}
	}
}
