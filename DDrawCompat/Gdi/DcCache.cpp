#include <memory>
#include <vector>

#include "Gdi/DcCache.h"
#include "Gdi/VirtualScreen.h"

namespace
{
	typedef std::unique_ptr<HDC__, void(*)(HDC)> CachedDc;

	thread_local std::vector<CachedDc> g_cache;
}

namespace Gdi
{
	namespace DcCache
	{
		void deleteDc(HDC cachedDc)
		{
			Gdi::VirtualScreen::deleteDc(cachedDc);
		}

		HDC getDc()
		{
			HDC cachedDc = nullptr;

			if (g_cache.empty())
			{
				cachedDc = Gdi::VirtualScreen::createDc();
			}
			else
			{
				cachedDc = g_cache.back().release();
				g_cache.pop_back();
			}

			return cachedDc;
		}

		void releaseDc(HDC cachedDc)
		{
			g_cache.emplace_back(CachedDc(cachedDc, &deleteDc));
		}
	}
}
