#include <algorithm>

#include "CompatDirectDraw.h"
#include "CompatDirectDrawPalette.h"
#include "CompatDirectDrawSurface.h"
#include "CompatPaletteConverter.h"
#include "CompatPrimarySurface.h"
#include "DDrawRepository.h"
#include "DDrawTypes.h"
#include "Hook.h"
#include "RealPrimarySurface.h"

namespace
{
	CRITICAL_SECTION g_criticalSection = {};
	HDC g_dc = nullptr;
	RGBQUAD g_halftonePalette[256] = {};
	HGDIOBJ g_oldBitmap = nullptr;
	IDirectDrawSurface7* g_surface = nullptr;

	void convertPaletteEntriesToRgbQuad(RGBQUAD (&entries)[256])
	{
		for (int i = 0; i < 256; ++i)
		{
			entries[i].rgbReserved = 0;
			std::swap(entries[i].rgbRed, entries[i].rgbBlue);
		}
	}

	HBITMAP createDibSection(void*& bits)
	{
		struct PalettizedBitmapInfo
		{
			BITMAPINFOHEADER header;
			PALETTEENTRY colors[256];
		};

		PalettizedBitmapInfo bmi = {};
		bmi.header.biSize = sizeof(bmi.header);
		bmi.header.biWidth = RealPrimarySurface::s_surfaceDesc.dwWidth;
		bmi.header.biHeight = -static_cast<LONG>(RealPrimarySurface::s_surfaceDesc.dwHeight);
		bmi.header.biPlanes = 1;
		bmi.header.biBitCount = 8;
		bmi.header.biCompression = BI_RGB;
		bmi.header.biClrUsed = 256;

		return CreateDIBSection(nullptr, reinterpret_cast<BITMAPINFO*>(&bmi),
			DIB_RGB_COLORS, &bits, nullptr, 0);
	}

	IDirectDrawSurface7* createSurface(void* bits)
	{
		IDirectDraw7* dd = DDrawRepository::getDirectDraw();
		if (!dd)
		{
			return nullptr;
		}

		DDSURFACEDESC2 desc = {};
		desc.dwSize = sizeof(desc);
		desc.dwFlags = DDSD_WIDTH | DDSD_HEIGHT | DDSD_PIXELFORMAT | DDSD_CAPS |
			DDSD_PITCH | DDSD_LPSURFACE;
		desc.dwWidth = RealPrimarySurface::s_surfaceDesc.dwWidth;
		desc.dwHeight = RealPrimarySurface::s_surfaceDesc.dwHeight;
		desc.ddpfPixelFormat = CompatPrimarySurface::displayMode.pixelFormat;
		desc.ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN | DDSCAPS_SYSTEMMEMORY;
		desc.lPitch = (RealPrimarySurface::s_surfaceDesc.dwWidth + 3) & ~3;
		desc.lpSurface = bits;

		IDirectDrawSurface7* surface = nullptr;
		CompatDirectDraw<IDirectDraw7>::s_origVtable.CreateSurface(dd, &desc, &surface, nullptr);
		return surface;
	}

	void initHalftonePalette()
	{
		HDC dc = GetDC(nullptr);
		HPALETTE palette = CreateHalftonePalette(dc);

		GetPaletteEntries(palette, 0, 256, reinterpret_cast<PALETTEENTRY*>(g_halftonePalette));
		convertPaletteEntriesToRgbQuad(g_halftonePalette);

		DeleteObject(palette);
		ReleaseDC(nullptr, dc);
	}
}

namespace CompatPaletteConverter
{
	bool create()
	{
		if (CompatPrimarySurface::displayMode.pixelFormat.dwRGBBitCount > 8 &&
			RealPrimarySurface::s_surfaceDesc.ddpfPixelFormat.dwRGBBitCount > 8)
		{
			return true;
		}

		void* bits = nullptr;
		HBITMAP dib = createDibSection(bits);
		if (!dib)
		{
			Compat::Log() << "Failed to create the palette converter DIB section";
			return false;
		}

		IDirectDrawSurface7* surface = createSurface(bits);
		if (!surface)
		{
			Compat::Log() << "Failed to create the palette converter surface";
			DeleteObject(dib);
			return false;
		}

	 	HDC dc = CALL_ORIG_FUNC(CreateCompatibleDC)(nullptr);
		if (!dc)
		{
			Compat::Log() << "Failed to create the palette converter DC";
			CompatDirectDrawSurface<IDirectDrawSurface7>::s_origVtable.Release(surface);
			DeleteObject(dib);
			return false;
		}

		EnterCriticalSection(&g_criticalSection);
		g_oldBitmap = SelectObject(dc, dib);
		g_dc = dc;
		g_surface = surface;
		LeaveCriticalSection(&g_criticalSection);
		return true;
	}

	void init()
	{
		InitializeCriticalSection(&g_criticalSection);
		initHalftonePalette();
	}

	HDC lockDc()
	{
		EnterCriticalSection(&g_criticalSection);
		return g_dc;
	}

	IDirectDrawSurface7* lockSurface()
	{
		EnterCriticalSection(&g_criticalSection);
		return g_surface;
	}

	void release()
	{
		EnterCriticalSection(&g_criticalSection);

		if (!g_surface)
		{
			LeaveCriticalSection(&g_criticalSection);
			return;
		}

		CompatDirectDrawSurface<IDirectDrawSurface7>::s_origVtable.Release(g_surface);
		g_surface = nullptr;

		DeleteObject(SelectObject(g_dc, g_oldBitmap));
		DeleteDC(g_dc);
		g_dc = nullptr;

		LeaveCriticalSection(&g_criticalSection);
	}

	void setClipper(IDirectDrawClipper* clipper)
	{
		EnterCriticalSection(&g_criticalSection);
		if (g_surface)
		{
			HRESULT result = CompatDirectDrawSurface<IDirectDrawSurface7>::s_origVtable.SetClipper(
				g_surface, clipper);
			if (FAILED(result))
			{
				LOG_ONCE("Failed to set a clipper on the palette converter surface: " << result);
			}
		}
		LeaveCriticalSection(&g_criticalSection);
	}

	void setPalette(IDirectDrawPalette* palette)
	{
		EnterCriticalSection(&g_criticalSection);
		if (g_dc)
		{
			if (palette)
			{
				RGBQUAD entries[256] = {};
				CompatDirectDrawPalette::s_origVtable.GetEntries(
					palette, 0, 0, 256, reinterpret_cast<PALETTEENTRY*>(entries));
				convertPaletteEntriesToRgbQuad(entries);
				SetDIBColorTable(g_dc, 0, 256, entries);
			}
			else
			{
				SetDIBColorTable(g_dc, 0, 256, g_halftonePalette);
			}
		}
		LeaveCriticalSection(&g_criticalSection);
	}

	void unlockDc()
	{
		LeaveCriticalSection(&g_criticalSection);
	}

	void unlockSurface()
	{
		LeaveCriticalSection(&g_criticalSection);
	}
};
