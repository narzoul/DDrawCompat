#define WIN32_LEAN_AND_MEAN

#include <set>

#include <Windows.h>

#include "Common/Hook.h"
#include "Common/Log.h"
#include "DDraw/ScopedThreadLock.h"
#include "DDraw/Surfaces/PrimarySurface.h"
#include "Gdi/Gdi.h"
#include "Gdi/Palette.h"
#include "VirtualScreen.h"
#include "Win32/DisplayMode.h"

namespace
{
	PALETTEENTRY g_systemPalette[256] = {};
	UINT g_systemPaletteUse = SYSPAL_STATIC;
	UINT g_systemPaletteFirstUnusedIndex = 10;
	UINT g_systemPaletteFirstNonReservedIndex = 10;
	UINT g_systemPaletteLastNonReservedIndex = 245;

	std::set<HDC> g_foregroundPaletteDcs;

	bool isSameColor(PALETTEENTRY entry1, PALETTEENTRY entry2)
	{
		return entry1.peRed == entry2.peRed &&
			entry1.peGreen == entry2.peGreen &&
			entry1.peBlue == entry2.peBlue;
	}

	bool exactMatch(PALETTEENTRY entry)
	{
		for (UINT i = 0; i < g_systemPaletteFirstUnusedIndex; ++i)
		{
			if (isSameColor(entry, g_systemPalette[i]))
			{
				return true;
			}
		}

		for (UINT i = g_systemPaletteLastNonReservedIndex + 1; i < 256; ++i)
		{
			if (isSameColor(entry, g_systemPalette[i]))
			{
				return true;
			}
		}

		return false;
	}

	UINT fillSystemPalette(HDC dc)
	{
		HPALETTE palette = reinterpret_cast<HPALETTE>(GetCurrentObject(dc, OBJ_PAL));
		if (!palette || GetStockObject(DEFAULT_PALETTE) == palette)
		{
			return 0;
		}

		PALETTEENTRY entries[256] = {};
		UINT count = GetPaletteEntries(palette, 0, 256, entries);
		for (UINT i = 0;
			i < count && g_systemPaletteFirstUnusedIndex <= g_systemPaletteLastNonReservedIndex;
			++i)
		{
			if ((entries[i].peFlags & PC_EXPLICIT) ||
				0 == (entries[i].peFlags & (PC_NOCOLLAPSE | PC_RESERVED)) && exactMatch(entries[i]))
			{
				continue;
			}

			g_systemPalette[g_systemPaletteFirstUnusedIndex] = entries[i];
			g_systemPalette[g_systemPaletteFirstUnusedIndex].peFlags = 0;
			++g_systemPaletteFirstUnusedIndex;
		}

		Gdi::VirtualScreen::updatePalette();
		return count;
	}

	UINT WINAPI getSystemPaletteEntries(HDC hdc, UINT iStartIndex, UINT nEntries, LPPALETTEENTRY lppe)
	{
		LOG_FUNC("GetSystemPaletteEntries", hdc, iStartIndex, nEntries, lppe);
		if (!Gdi::isDisplayDc(hdc))
		{
			return LOG_RESULT(0);
		}

		if (!lppe)
		{
			return LOG_RESULT(256);
		}

		if (iStartIndex >= 256)
		{
			return LOG_RESULT(0);
		}

		if (nEntries > 256 - iStartIndex)
		{
			nEntries = 256 - iStartIndex;
		}

		DDraw::ScopedThreadLock lock;
		std::memcpy(lppe, &DDraw::PrimarySurface::s_paletteEntries[iStartIndex], nEntries * sizeof(PALETTEENTRY));

		return LOG_RESULT(nEntries);
	}

	UINT WINAPI getSystemPaletteUse(HDC hdc)
	{
		LOG_FUNC("GetSystemPaletteUse", hdc);
		if (!Gdi::isDisplayDc(hdc))
		{
			return LOG_RESULT(SYSPAL_ERROR);
		}
		DDraw::ScopedThreadLock lock;
		return g_systemPaletteUse;
	}

	UINT WINAPI realizePalette(HDC hdc)
	{
		LOG_FUNC("RealizePalette", hdc);
		if (Gdi::isDisplayDc(hdc))
		{
			DDraw::ScopedThreadLock lock;
			if (g_foregroundPaletteDcs.find(hdc) != g_foregroundPaletteDcs.end())
			{
				g_systemPaletteFirstUnusedIndex = g_systemPaletteFirstNonReservedIndex;
			}
			return LOG_RESULT(fillSystemPalette(hdc));
		}
		return LOG_RESULT(CALL_ORIG_FUNC(RealizePalette)(hdc));
	}

	int WINAPI releaseDc(HWND hWnd, HDC hDC)
	{
		LOG_FUNC("ReleaseDC", hWnd, hDC);
		DDraw::ScopedThreadLock lock;
		g_foregroundPaletteDcs.erase(hDC);
		return LOG_RESULT(CALL_ORIG_FUNC(ReleaseDC)(hWnd, hDC));
	}

	HPALETTE WINAPI selectPalette(HDC hdc, HPALETTE hpal, BOOL bForceBackground)
	{
		LOG_FUNC("SelectPalette", hdc, hpal, bForceBackground);
		HPALETTE result = CALL_ORIG_FUNC(SelectPalette)(hdc, hpal, bForceBackground);
		if (result && Gdi::isDisplayDc(hdc))
		{
			HWND wnd = CALL_ORIG_FUNC(WindowFromDC)(hdc);
			if (wnd && GetDesktopWindow() != wnd)
			{
				DDraw::ScopedThreadLock lock;
				if (bForceBackground || GetStockObject(DEFAULT_PALETTE) == hpal)
				{
					g_foregroundPaletteDcs.erase(hdc);
				}
				else
				{
					g_foregroundPaletteDcs.insert(hdc);
				}
			}
		}
		return LOG_RESULT(result);
	}

	UINT WINAPI setSystemPaletteUse(HDC hdc, UINT uUsage)
	{
		LOG_FUNC("SetSystemPaletteUse", hdc, uUsage);
		if (!Gdi::isDisplayDc(hdc))
		{
			return LOG_RESULT(SYSPAL_ERROR);
		}

		DDraw::ScopedThreadLock lock;
		const UINT prevUsage = g_systemPaletteUse;
		switch (uUsage)
		{
		case SYSPAL_STATIC:
			g_systemPaletteFirstNonReservedIndex = 10;
			g_systemPaletteLastNonReservedIndex = 245;
			break;

		case SYSPAL_NOSTATIC:
			g_systemPaletteFirstNonReservedIndex = 1;
			g_systemPaletteLastNonReservedIndex = 254;
			break;

		case SYSPAL_NOSTATIC256:
			g_systemPaletteFirstNonReservedIndex = 0;
			g_systemPaletteLastNonReservedIndex = 255;
			break;

		default:
			return LOG_RESULT(SYSPAL_ERROR);
		}

		g_systemPaletteUse = uUsage;
		return LOG_RESULT(prevUsage);
	}
}

namespace Gdi
{
	namespace Palette
	{
		void installHooks()
		{
			HPALETTE defaultPalette = reinterpret_cast<HPALETTE>(GetStockObject(DEFAULT_PALETTE));
			GetPaletteEntries(defaultPalette, 0, 10, g_systemPalette);
			GetPaletteEntries(defaultPalette, 246, 10, &g_systemPalette[246]);
			Gdi::VirtualScreen::updatePalette();

			HOOK_FUNCTION(gdi32, GetSystemPaletteEntries, getSystemPaletteEntries);
			HOOK_FUNCTION(gdi32, GetSystemPaletteUse, getSystemPaletteUse);
			HOOK_FUNCTION(gdi32, RealizePalette, realizePalette);
			HOOK_FUNCTION(user32, ReleaseDC, releaseDc);
			HOOK_FUNCTION(gdi32, SelectPalette, selectPalette);
			HOOK_FUNCTION(gdi32, SetSystemPaletteUse, setSystemPaletteUse);
		}
	}
}
