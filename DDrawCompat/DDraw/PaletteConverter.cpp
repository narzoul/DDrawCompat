#include <algorithm>
#include <cstring>

#include "Common/CompatPtr.h"
#include "Common/Hook.h"
#include "Common/ScopedCriticalSection.h"
#include "DDraw/CompatPrimarySurface.h"
#include "DDraw/DisplayMode.h"
#include "DDraw/PaletteConverter.h"
#include "DDraw/RealPrimarySurface.h"
#include "DDraw/Repository.h"
#include "DDraw/Types.h"

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

	HBITMAP createDibSection(const DDSURFACEDESC2& dm, void*& bits)
	{
		struct PalettizedBitmapInfo
		{
			BITMAPINFOHEADER header;
			PALETTEENTRY colors[256];
		};

		PalettizedBitmapInfo bmi = {};
		bmi.header.biSize = sizeof(bmi.header);
		bmi.header.biWidth = dm.dwWidth;
		bmi.header.biHeight = -static_cast<LONG>(dm.dwHeight);
		bmi.header.biPlanes = 1;
		bmi.header.biBitCount = static_cast<WORD>(dm.ddpfPixelFormat.dwRGBBitCount);
		bmi.header.biCompression = BI_RGB;

		return CreateDIBSection(nullptr, reinterpret_cast<BITMAPINFO*>(&bmi),
			DIB_RGB_COLORS, &bits, nullptr, 0);
	}

	CompatPtr<IDirectDrawSurface7> createSurface(const DDSURFACEDESC2& dm, void* bits)
	{
		DDSURFACEDESC2 desc = {};
		desc.dwSize = sizeof(desc);
		desc.dwFlags = DDSD_WIDTH | DDSD_HEIGHT | DDSD_PIXELFORMAT | DDSD_CAPS |
			DDSD_PITCH | DDSD_LPSURFACE;
		desc.dwWidth = dm.dwWidth;
		desc.dwHeight = dm.dwHeight;
		desc.ddpfPixelFormat = dm.ddpfPixelFormat;
		desc.ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN | DDSCAPS_SYSTEMMEMORY;
		desc.lPitch = (dm.dwWidth * dm.ddpfPixelFormat.dwRGBBitCount / 8 + 3) & ~3;
		desc.lpSurface = bits;

		auto dd(DDraw::Repository::getDirectDraw());
		CompatPtr<IDirectDrawSurface7> surface;
		dd->CreateSurface(dd, &desc, &surface.getRef(), nullptr);
		return surface;
	}
}

namespace DDraw
{
	namespace PaletteConverter
	{
		bool create()
		{
			auto dd(Repository::getDirectDraw());
			auto dm(DisplayMode::getDisplayMode(*dd));
			DDSURFACEDESC2 realDm = {};
			realDm.dwSize = sizeof(realDm);
			dd->GetDisplayMode(dd, &realDm);
			if (dm.ddpfPixelFormat.dwRGBBitCount > 8 &&
				realDm.ddpfPixelFormat.dwRGBBitCount > 8)
			{
				return true;
			}

			void* bits = nullptr;
			HBITMAP dib = createDibSection(dm, bits);
			if (!dib)
			{
				Compat::Log() << "Failed to create the palette converter DIB section";
				return false;
			}

			CompatPtr<IDirectDrawSurface7> surface(createSurface(dm, bits));
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

		void setClipper(CompatWeakPtr<IDirectDrawClipper> clipper)
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
			if (g_dc && CompatPrimarySurface::g_palette)
			{
				RGBQUAD entries[256] = {};
				std::memcpy(entries, &CompatPrimarySurface::g_paletteEntries[startingEntry],
					count * sizeof(PALETTEENTRY));
				convertPaletteEntriesToRgbQuad(entries, count);
				SetDIBColorTable(g_dc, startingEntry, count, entries);
			}
		}
	};
}
