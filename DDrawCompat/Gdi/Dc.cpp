#include <algorithm>
#include <unordered_map>
#include <vector>

#include "Common/Hook.h"
#include "Common/Log.h"
#include "Common/ScopedCriticalSection.h"
#include "Gdi/Dc.h"
#include "Gdi/DcCache.h"
#include "Gdi/Gdi.h"
#include "Gdi/VirtualScreen.h"
#include "Gdi/Window.h"

namespace
{
	struct CompatDc
	{
		HDC dc;
		DWORD refCount;
		HDC origDc;
		int savedState;
	};

	typedef std::unique_ptr<HDC__, void(*)(HDC)> OrigDc;
	typedef std::unordered_map<HDC, CompatDc> CompatDcMap;

	CRITICAL_SECTION g_cs;
	CompatDcMap g_origDcToCompatDc;
	thread_local std::vector<OrigDc> g_threadDcs;

	void copyDcAttributes(const CompatDc& compatDc, HDC origDc, const POINT& origin)
	{
		SelectObject(compatDc.dc, GetCurrentObject(origDc, OBJ_FONT));
		SelectObject(compatDc.dc, GetCurrentObject(origDc, OBJ_BRUSH));
		SelectObject(compatDc.dc, GetCurrentObject(origDc, OBJ_PEN));

		if (GM_ADVANCED == GetGraphicsMode(origDc))
		{
			SetGraphicsMode(compatDc.dc, GM_ADVANCED);
			XFORM transform = {};
			GetWorldTransform(origDc, &transform);
			SetWorldTransform(compatDc.dc, &transform);
		}

		SetMapMode(compatDc.dc, GetMapMode(origDc));

		POINT viewportOrg = {};
		GetViewportOrgEx(origDc, &viewportOrg);
		SetViewportOrgEx(compatDc.dc, viewportOrg.x + origin.x, viewportOrg.y + origin.y, nullptr);
		SIZE viewportExt = {};
		GetViewportExtEx(origDc, &viewportExt);
		SetViewportExtEx(compatDc.dc, viewportExt.cx, viewportExt.cy, nullptr);

		POINT windowOrg = {};
		GetWindowOrgEx(origDc, &windowOrg);
		SetWindowOrgEx(compatDc.dc, windowOrg.x, windowOrg.y, nullptr);
		SIZE windowExt = {};
		GetWindowExtEx(origDc, &windowExt);
		SetWindowExtEx(compatDc.dc, windowExt.cx, windowExt.cy, nullptr);

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

	void deleteDc(HDC origDc)
	{
		Compat::ScopedCriticalSection lock(g_cs);
		auto it = g_origDcToCompatDc.find(origDc);
		RestoreDC(it->second.dc, it->second.savedState);
		Gdi::DcCache::deleteDc(it->second.dc);
		g_origDcToCompatDc.erase(origDc);
	}

	void setClippingRegion(HDC compatDc, HDC origDc, HWND hwnd, const POINT& origin)
	{
		if (hwnd)
		{
			HRGN sysRgn = CreateRectRgn(0, 0, 0, 0);
			if (1 == GetRandomRgn(origDc, sysRgn, SYSRGN))
			{
				SelectClipRgn(compatDc, sysRgn);
				SetMetaRgn(compatDc);
			}
			DeleteObject(sysRgn);
		}

		HRGN clipRgn = CreateRectRgn(0, 0, 0, 0);
		if (1 == GetClipRgn(origDc, clipRgn))
		{
			OffsetRgn(clipRgn, origin.x, origin.y);
			SelectClipRgn(compatDc, clipRgn);
		}
		DeleteObject(clipRgn);
	}

	void updateWindow(HWND wnd)
	{
		auto window = Gdi::Window::get(wnd);
		if (!window)
		{
			return;
		}

		RECT windowRect = {};
		GetWindowRect(wnd, &windowRect);

		RECT cachedWindowRect = window->getWindowRect();
		if (!EqualRect(&windowRect, &cachedWindowRect))
		{
			Gdi::Window::updateAll();
		}
	}
}

namespace Gdi
{
	namespace Dc
	{
		HDC getDc(HDC origDc)
		{
			if (!origDc || OBJ_DC != GetObjectType(origDc) || DT_RASDISPLAY != GetDeviceCaps(origDc, TECHNOLOGY))
			{
				return nullptr;
			}

			Compat::ScopedCriticalSection lock(g_cs);
			auto it = g_origDcToCompatDc.find(origDc);
			if (it != g_origDcToCompatDc.end())
			{
				++it->second.refCount;
				return it->second.dc;
			}

			const HWND wnd = CALL_ORIG_FUNC(WindowFromDC)(origDc);
			const HWND rootWnd = wnd ? GetAncestor(wnd, GA_ROOT) : nullptr;
			if (rootWnd && GetDesktopWindow() != rootWnd)
			{
				updateWindow(rootWnd);
			}

			CompatDc compatDc;
			compatDc.dc = Gdi::DcCache::getDc();
			if (!compatDc.dc)
			{
				return nullptr;
			}

			POINT origin = {};
			GetDCOrgEx(origDc, &origin);
			RECT virtualScreenBounds = Gdi::VirtualScreen::getBounds();
			origin.x -= virtualScreenBounds.left;
			origin.y -= virtualScreenBounds.top;

			compatDc.savedState = SaveDC(compatDc.dc);
			copyDcAttributes(compatDc, origDc, origin);
			setClippingRegion(compatDc.dc, origDc, wnd, origin);

			compatDc.refCount = 1;
			compatDc.origDc = origDc;
			g_origDcToCompatDc.insert(CompatDcMap::value_type(origDc, compatDc));
			g_threadDcs.emplace_back(origDc, &deleteDc);

			return compatDc.dc;
		}

		HDC getOrigDc(HDC dc)
		{
			Compat::ScopedCriticalSection lock(g_cs);
			const auto it = std::find_if(g_origDcToCompatDc.begin(), g_origDcToCompatDc.end(),
				[dc](const CompatDcMap::value_type& compatDc) { return compatDc.second.dc == dc; });
			return it != g_origDcToCompatDc.end() ? it->first : dc;
		}

		void init()
		{
			InitializeCriticalSection(&g_cs);
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
				auto threadDcIter = std::find_if(g_threadDcs.begin(), g_threadDcs.end(),
					[origDc](const OrigDc& dc) { return dc.get() == origDc; });
				threadDcIter->release();
				g_threadDcs.erase(threadDcIter);

				RestoreDC(compatDc.dc, compatDc.savedState);
				Gdi::DcCache::releaseDc(compatDc.dc);
				g_origDcToCompatDc.erase(origDc);
			}
		}
	}
}
