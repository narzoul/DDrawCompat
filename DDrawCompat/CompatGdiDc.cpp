#include "CompatGdiDc.h"
#include "CompatGdiDcCache.h"
#include "RealPrimarySurface.h"

namespace
{
	struct ExcludeClipRectsData
	{
		HDC compatDc;
		POINT origin;
		HWND rootWnd;
	};

	BOOL CALLBACK excludeClipRectsForOverlappingWindows(HWND hwnd, LPARAM lParam)
	{
		auto excludeClipRectsData = reinterpret_cast<ExcludeClipRectsData*>(lParam);
		if (hwnd == excludeClipRectsData->rootWnd)
		{
			return FALSE;
		}

		RECT rect = {};
		GetWindowRect(hwnd, &rect);
		OffsetRect(&rect, -excludeClipRectsData->origin.x, -excludeClipRectsData->origin.y);
		ExcludeClipRect(excludeClipRectsData->compatDc, rect.left, rect.top, rect.right, rect.bottom);
		return TRUE;
	}
}

namespace CompatGdiDc
{
	CachedDc getDc(HDC origDc)
	{
		CachedDc cachedDc = {};
		if (!origDc || !RealPrimarySurface::isFullScreen() ||
			OBJ_DC != GetObjectType(origDc) ||
			DT_RASDISPLAY != GetDeviceCaps(origDc, TECHNOLOGY))
		{
			return cachedDc;
		}

		cachedDc = CompatGdiDcCache::getDc();
		if (!cachedDc.dc)
		{
			return cachedDc;
		}

		HWND hwnd = WindowFromDC(origDc);
		if (hwnd)
		{
			POINT origin = {};
			GetDCOrgEx(origDc, &origin);
			SetWindowOrgEx(cachedDc.dc, -origin.x, -origin.y, nullptr);

			HRGN clipRgn = CreateRectRgn(0, 0, 0, 0);
			GetRandomRgn(origDc, clipRgn, SYSRGN);
			SelectClipRgn(cachedDc.dc, clipRgn);
			RECT r = {};
			GetRgnBox(clipRgn, &r);
			DeleteObject(clipRgn);

			ExcludeClipRectsData excludeClipRectsData = { cachedDc.dc, origin, GetAncestor(hwnd, GA_ROOT) };
			EnumThreadWindows(GetCurrentThreadId(), &excludeClipRectsForOverlappingWindows,
				reinterpret_cast<LPARAM>(&excludeClipRectsData));
		}

		return cachedDc;
	}

	void releaseDc(const CachedDc& cachedDc)
	{
		CompatGdiDcCache::releaseDc(cachedDc);
	}
}
