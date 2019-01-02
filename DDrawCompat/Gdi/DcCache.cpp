#include <algorithm>
#include <map>
#include <memory>
#include <vector>

#include "Common/ScopedCriticalSection.h"
#include "Gdi/DcCache.h"
#include "Gdi/VirtualScreen.h"

namespace
{
	Compat::CriticalSection g_cs;
	std::map<DWORD, std::vector<HDC>> g_threadIdToDcCache;
}

namespace Gdi
{
	namespace DcCache
	{
		void deleteDc(HDC cachedDc)
		{
			Compat::ScopedCriticalSection lock(g_cs);
			for (auto& threadIdToDcCache : g_threadIdToDcCache)
			{
				auto& dcCache = threadIdToDcCache.second;
				auto it = std::find(dcCache.begin(), dcCache.end(), cachedDc);
				if (it != dcCache.end())
				{
					Gdi::VirtualScreen::deleteDc(*it);
					dcCache.erase(it);
					return;
				}
			}
		}

		void dllProcessDetach()
		{
			Compat::ScopedCriticalSection lock(g_cs);
			for (auto& threadIdToDcCache : g_threadIdToDcCache)
			{
				for (HDC dc : threadIdToDcCache.second)
				{
					Gdi::VirtualScreen::deleteDc(dc);
				}
			}
			g_threadIdToDcCache.clear();
		}

		void dllThreadDetach()
		{
			Compat::ScopedCriticalSection lock(g_cs);
			auto it = g_threadIdToDcCache.find(GetCurrentThreadId());
			if (it == g_threadIdToDcCache.end())
			{
				return;
			}

			for (HDC dc : it->second)
			{
				Gdi::VirtualScreen::deleteDc(dc);
			}

			g_threadIdToDcCache.erase(it);
		}

		HDC getDc()
		{
			Compat::ScopedCriticalSection lock(g_cs);
			std::vector<HDC>& dcCache = g_threadIdToDcCache[GetCurrentThreadId()];

			if (dcCache.empty())
			{
				return Gdi::VirtualScreen::createDc();
			}

			HDC dc = dcCache.back();
			dcCache.pop_back();
			return dc;
		}

		void releaseDc(HDC cachedDc)
		{
			Compat::ScopedCriticalSection lock(g_cs);
			g_threadIdToDcCache[GetCurrentThreadId()].push_back(cachedDc);
		}
	}
}
