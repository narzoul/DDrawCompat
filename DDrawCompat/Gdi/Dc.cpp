#include <algorithm>
#include <map>
#include <vector>

#include <Common/Hook.h>
#include <Common/Log.h>
#include <Common/ScopedCriticalSection.h>
#include <D3dDdi/ScopedCriticalSection.h>
#include <Gdi/Dc.h>
#include <Gdi/Gdi.h>
#include <Gdi/Region.h>
#include <Gdi/VirtualScreen.h>
#include <Gdi/Window.h>

namespace
{
	struct Cache
	{
		std::vector<std::unique_ptr<HDC__, void(*)(HDC)>> cache;
		std::vector<std::unique_ptr<HDC__, void(*)(HDC)>> defPalCache;
	};

	struct CompatDc
	{
		HDC dc;
		DWORD refCount;
		HDC origDc;
		DWORD threadId;
		int savedState;
		bool useDefaultPalette;
	};

	typedef std::map<HDC, CompatDc> CompatDcMap;

	Compat::CriticalSection g_cs;
	CompatDcMap g_origDcToCompatDc;
	std::map<DWORD, Cache> g_threadIdToDcCache;

	void restoreDc(const CompatDc& compatDc);

	void copyDcAttributes(CompatDc& compatDc, HDC origDc, const POINT& origin)
	{
		SelectObject(compatDc.dc, GetCurrentObject(origDc, OBJ_FONT));
		SelectObject(compatDc.dc, GetCurrentObject(origDc, OBJ_BRUSH));
		SelectObject(compatDc.dc, GetCurrentObject(origDc, OBJ_PEN));
		CALL_ORIG_FUNC(SelectPalette)(compatDc.dc, static_cast<HPALETTE>(GetCurrentObject(origDc, OBJ_PAL)), FALSE);

		const int graphicsMode = GetGraphicsMode(origDc);
		SetGraphicsMode(compatDc.dc, graphicsMode);
		if (GM_ADVANCED == graphicsMode)
		{
			XFORM transform = {};
			GetWorldTransform(origDc, &transform);
			SetWorldTransform(compatDc.dc, &transform);
		}

		const int mapMode = GetMapMode(origDc);
		SetMapMode(compatDc.dc, mapMode);
		if (MM_TEXT != mapMode)
		{
			SIZE windowExt = {};
			GetWindowExtEx(origDc, &windowExt);
			SetWindowExtEx(compatDc.dc, windowExt.cx, windowExt.cy, nullptr);

			SIZE viewportExt = {};
			GetViewportExtEx(origDc, &viewportExt);
			SetViewportExtEx(compatDc.dc, viewportExt.cx, viewportExt.cy, nullptr);
		}

		POINT windowOrg = {};
		GetWindowOrgEx(origDc, &windowOrg);
		SetWindowOrgEx(compatDc.dc, windowOrg.x, windowOrg.y, nullptr);

		POINT viewportOrg = {};
		GetViewportOrgEx(origDc, &viewportOrg);
		SetViewportOrgEx(compatDc.dc, viewportOrg.x + origin.x, viewportOrg.y + origin.y, nullptr);

		SetArcDirection(compatDc.dc, GetArcDirection(origDc));
		SetBkColor(compatDc.dc, GetBkColor(origDc));
		SetBkMode(compatDc.dc, GetBkMode(origDc));
		SetDCBrushColor(compatDc.dc, GetDCBrushColor(origDc));
		SetDCPenColor(compatDc.dc, GetDCPenColor(origDc));
		SetLayout(compatDc.dc, GetLayout(origDc));
		SetPolyFillMode(compatDc.dc, GetPolyFillMode(origDc));
		SetROP2(compatDc.dc, GetROP2(origDc));
		SetStretchBltMode(compatDc.dc, GetStretchBltMode(origDc));
		SetTextAlign(compatDc.dc, GetTextAlign(origDc));
		SetTextCharacterExtra(compatDc.dc, GetTextCharacterExtra(origDc));
		SetTextColor(compatDc.dc, GetTextColor(origDc));

		POINT brushOrg = {};
		GetBrushOrgEx(origDc, &brushOrg);
		SetBrushOrgEx(compatDc.dc, brushOrg.x, brushOrg.y, nullptr);

		POINT currentPos = {};
		GetCurrentPositionEx(origDc, &currentPos);
		MoveToEx(compatDc.dc, currentPos.x, currentPos.y, nullptr);
	}

	void restoreDc(const CompatDc& compatDc)
	{
		// Bitmap may have changed during VirtualScreen::update, do not let RestoreDC restore the old one
		HGDIOBJ bitmap = GetCurrentObject(compatDc.dc, OBJ_BITMAP);
		RestoreDC(compatDc.dc, compatDc.savedState);
		SelectObject(compatDc.dc, bitmap);
	}

	void setClippingRegion(const CompatDc& compatDc, HWND hwnd, const POINT& origin, const RECT& virtualScreenBounds)
	{
		if (hwnd)
		{
			Gdi::Region sysRgn;
			GetRandomRgn(compatDc.origDc, sysRgn, SYSRGN);
			OffsetRgn(sysRgn, -virtualScreenBounds.left, -virtualScreenBounds.top);
			SelectClipRgn(compatDc.dc, sysRgn);
		}
		else
		{
			SelectClipRgn(compatDc.dc, nullptr);
		}

		Gdi::Region clipRgn;
		if (1 == GetClipRgn(compatDc.origDc, clipRgn))
		{
			OffsetRgn(clipRgn, origin.x, origin.y);
			ExtSelectClipRgn(compatDc.dc, clipRgn, RGN_AND);
		}

		SetMetaRgn(compatDc.dc);
	}
}

namespace Gdi
{
	namespace Dc
	{
		void dllProcessDetach()
		{
			Compat::ScopedCriticalSection lock(g_cs);
			for (auto& origDcToCompatDc : g_origDcToCompatDc)
			{
				restoreDc(origDcToCompatDc.second);
				Gdi::VirtualScreen::deleteDc(origDcToCompatDc.second.dc);
			}
			g_origDcToCompatDc.clear();
			g_threadIdToDcCache.clear();
		}

		void dllThreadDetach()
		{
			Compat::ScopedCriticalSection lock(g_cs);
			const DWORD threadId = GetCurrentThreadId();
			auto it = g_origDcToCompatDc.begin();
			while (it != g_origDcToCompatDc.end())
			{
				if (threadId == it->second.threadId)
				{
					restoreDc(it->second);
					Gdi::VirtualScreen::deleteDc(it->second.dc);
					it = g_origDcToCompatDc.erase(it);
				}
				else
				{
					++it;
				}
			}
			g_threadIdToDcCache.erase(GetCurrentThreadId());
		}

		HDC getDc(HDC origDc)
		{
			if (!isDisplayDc(origDc))
			{
				return nullptr;
			}

			RECT virtualScreenBounds = Gdi::VirtualScreen::getBounds();

			D3dDdi::ScopedCriticalSection driverLock;
			Compat::ScopedCriticalSection lock(g_cs);
			auto it = g_origDcToCompatDc.find(origDc);
			if (it != g_origDcToCompatDc.end())
			{
				++it->second.refCount;
				return it->second.dc;
			}

			CompatDc compatDc;
			compatDc.useDefaultPalette = GetStockObject(DEFAULT_PALETTE) == GetCurrentObject(origDc, OBJ_PAL);

			auto& cache = g_threadIdToDcCache[GetCurrentThreadId()];
			auto& dcCache = compatDc.useDefaultPalette ? cache.defPalCache : cache.cache;
			if (dcCache.empty())
			{
				compatDc.dc = Gdi::VirtualScreen::createDc(compatDc.useDefaultPalette);
				if (!compatDc.dc)
				{
					return nullptr;
				}
			}
			else
			{
				compatDc.dc = dcCache.back().release();
				dcCache.pop_back();
			}

			POINT origin = {};
			GetDCOrgEx(origDc, &origin);
			HWND hwnd = CALL_ORIG_FUNC(WindowFromDC)(origDc);
			if (hwnd)
			{
				if (GetDesktopWindow() == hwnd)
				{
					hwnd = nullptr;
				}
				else
				{
					origin.x -= virtualScreenBounds.left;
					origin.y -= virtualScreenBounds.top;
				}
			}

			compatDc.refCount = 1;
			compatDc.origDc = origDc;
			compatDc.threadId = GetCurrentThreadId();
			compatDc.savedState = SaveDC(compatDc.dc);
			copyDcAttributes(compatDc, origDc, origin);
			setClippingRegion(compatDc, hwnd, origin, virtualScreenBounds);

			g_origDcToCompatDc.insert(CompatDcMap::value_type(origDc, compatDc));

			return compatDc.dc;
		}

		HDC getOrigDc(HDC dc)
		{
			Compat::ScopedCriticalSection lock(g_cs);
			const auto it = std::find_if(g_origDcToCompatDc.begin(), g_origDcToCompatDc.end(),
				[dc](const CompatDcMap::value_type& compatDc) { return compatDc.second.dc == dc; });
			return it != g_origDcToCompatDc.end() ? it->first : dc;
		}

		void releaseDc(HDC origDc)
		{
			Compat::ScopedCriticalSection lock(g_cs);
			auto it = g_origDcToCompatDc.find(origDc);
			if (it == g_origDcToCompatDc.end())
			{
				return;
			}

			CompatDc& compatDc = it->second;
			--compatDc.refCount;
			if (0 == compatDc.refCount)
			{
				restoreDc(compatDc);
				auto& cache = g_threadIdToDcCache[GetCurrentThreadId()];
				auto& dcCache = compatDc.useDefaultPalette ? cache.defPalCache : cache.cache;
				dcCache.emplace_back(compatDc.dc, Gdi::VirtualScreen::deleteDc);
				g_origDcToCompatDc.erase(origDc);
			}
		}
	}
}
