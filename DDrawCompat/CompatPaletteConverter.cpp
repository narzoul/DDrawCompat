#include <algorithm>
#include <cstring>

#include "CompatDirectDrawPalette.h"
#include "CompatPaletteConverter.h"
#include "CompatPrimarySurface.h"
#include "CompatPtr.h"
#include "DDrawRepository.h"
#include "DDrawTypes.h"
#include "Hook.h"
#include "RealPrimarySurface.h"
#include "ScopedCriticalSection.h"

namespace
{
	HDC g_dc = nullptr;
	HGDIOBJ g_oldBitmap = nullptr;
	CompatWeakPtr<IDirectDrawSurface7> g_surface;

	void convertPaletteEntriesToRgbQuad(RGBQUAD* entries, DWORD count)
	{
		for (DWORD i = 0; i < count; ++i)
		{
			entries[i].rgbReserved = 0;
			std::swap(entries[i].rgbRed, entries[i].rgbBlue);
		}
	}

	HBITMAP createDibSection(const DDSURFACEDESC2& primaryDesc, void*& bits)
	{
		struct PalettizedBitmapInfo
		{
			BITMAPINFOHEADER header;
			PALETTEENTRY colors[256];
		};

		PalettizedBitmapInfo bmi = {};
		bmi.header.biSize = sizeof(bmi.header);
		bmi.header.biWidth = primaryDesc.dwWidth;
		bmi.header.biHeight = -static_cast<LONG>(primaryDesc.dwHeight);
		bmi.header.biPlanes = 1;
		bmi.header.biBitCount = 8;
		bmi.header.biCompression = BI_RGB;
		bmi.header.biClrUsed = 256;

		return CreateDIBSection(nullptr, reinterpret_cast<BITMAPINFO*>(&bmi),
			DIB_RGB_COLORS, &bits, nullptr, 0);
	}

	CompatPtr<IDirectDrawSurface7> createSurface(const DDSURFACEDESC2& primaryDesc, void* bits)
	{
		DDSURFACEDESC2 desc = {};
		desc.dwSize = sizeof(desc);
		desc.dwFlags = DDSD_WIDTH | DDSD_HEIGHT | DDSD_PIXELFORMAT | DDSD_CAPS |
			DDSD_PITCH | DDSD_LPSURFACE;
		desc.dwWidth = primaryDesc.dwWidth;
		desc.dwHeight = primaryDesc.dwHeight;
		desc.ddpfPixelFormat = CompatPrimarySurface::displayMode.pixelFormat;
		desc.ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN | DDSCAPS_SYSTEMMEMORY;
		desc.lPitch = (primaryDesc.dwWidth + 3) & ~3;
		desc.lpSurface = bits;

		auto dd(DDrawRepository::getDirectDraw());
		CompatPtr<IDirectDrawSurface7> surface;
		dd->CreateSurface(dd, &desc, &surface.getRef(), nullptr);
		return surface;
	}
}

namespace CompatPaletteConverter
{
	bool create(const DDSURFACEDESC2& primaryDesc)
	{
		if (CompatPrimarySurface::displayMode.pixelFormat.dwRGBBitCount > 8 &&
			primaryDesc.ddpfPixelFormat.dwRGBBitCount > 8)
		{
			return true;
		}

		void* bits = nullptr;
		HBITMAP dib = createDibSection(primaryDesc, bits);
		if (!dib)
		{
			Compat::Log() << "Failed to create the palette converter DIB section";
			return false;
		}

		CompatPtr<IDirectDrawSurface7> surface(createSurface(primaryDesc, bits));
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
			DeleteObject(dib);
			return false;
		}

		g_oldBitmap = SelectObject(dc, dib);
		g_dc = dc;
		g_surface = surface.detach();
		return true;
	}

	HDC getDc()
	{
		return g_dc;
	}

	CompatWeakPtr<IDirectDrawSurface7> getSurface()
	{
		return g_surface;
	}

	void release()
	{
		if (!g_surface)
		{
			return;
		}

		g_surface.release();

		DeleteObject(SelectObject(g_dc, g_oldBitmap));
		DeleteDC(g_dc);
		g_dc = nullptr;
	}

	void setClipper(IDirectDrawClipper* clipper)
	{
		if (g_surface)
		{
			HRESULT result = g_surface->SetClipper(g_surface, clipper);
			if (FAILED(result))
			{
				LOG_ONCE("Failed to set a clipper on the palette converter surface: " << result);
			}
		}
	}

	void updatePalette(DWORD startingEntry, DWORD count)
	{
		if (g_dc && CompatPrimarySurface::palette)
		{
			RGBQUAD entries[256] = {};
			std::memcpy(entries, &CompatPrimarySurface::paletteEntries[startingEntry],
				count * sizeof(PALETTEENTRY));
			convertPaletteEntriesToRgbQuad(entries, count);
			SetDIBColorTable(g_dc, startingEntry, count, entries);
		}
	}
};
