#include <set>

#include "Common/ScopedCriticalSection.h"
#include "DDraw/DirectDraw.h"
#include "DDraw/Repository.h"
#include "Gdi/Gdi.h"
#include "Gdi/Region.h"
#include "Gdi/VirtualScreen.h"
#include "Win32/DisplayMode.h"

namespace
{
	CRITICAL_SECTION g_cs = {};
	Gdi::Region g_region;
	RECT g_bounds = {};
	DWORD g_bpp = 0;
	LONG g_width = 0;
	LONG g_height = 0;
	DWORD g_pitch = 0;
	HANDLE g_surfaceFileMapping = nullptr;
	void* g_surfaceView = nullptr;

	HGDIOBJ g_stockBitmap = nullptr;
	RGBQUAD g_systemPalette[256] = {};
	std::set<HDC> g_dcs;

	BOOL CALLBACK addMonitorRectToRegion(
		HMONITOR /*hMonitor*/, HDC /*hdcMonitor*/, LPRECT lprcMonitor, LPARAM dwData)
	{
		Gdi::Region& virtualScreenRegion = *reinterpret_cast<Gdi::Region*>(dwData);
		Gdi::Region monitorRegion(*lprcMonitor);
		virtualScreenRegion |= monitorRegion;
		return TRUE;
	}

	HBITMAP createDibSection(DWORD width, DWORD height, HANDLE section)
	{
		struct BITMAPINFO256 : public BITMAPINFO
		{
			RGBQUAD bmiRemainingColors[255];
		};

		BITMAPINFO256 bmi = {};
		bmi.bmiHeader.biSize = sizeof(bmi.bmiHeader);
		bmi.bmiHeader.biWidth = width;
		bmi.bmiHeader.biHeight = -static_cast<LONG>(height);
		bmi.bmiHeader.biPlanes = 1;
		bmi.bmiHeader.biBitCount = static_cast<WORD>(g_bpp);
		bmi.bmiHeader.biCompression = 8 == g_bpp ? BI_RGB : BI_BITFIELDS;

		if (8 == g_bpp)
		{
			memcpy(bmi.bmiColors, g_systemPalette, sizeof(g_systemPalette));
		}
		else
		{
			const auto pf = DDraw::getRgbPixelFormat(g_bpp);
			reinterpret_cast<DWORD&>(bmi.bmiColors[0]) = pf.dwRBitMask;
			reinterpret_cast<DWORD&>(bmi.bmiColors[1]) = pf.dwGBitMask;
			reinterpret_cast<DWORD&>(bmi.bmiColors[2]) = pf.dwBBitMask;
		}

		void* bits = nullptr;
		return CreateDIBSection(nullptr, &bmi, DIB_RGB_COLORS, &bits, section, 0);
	}
}

namespace Gdi
{
	namespace VirtualScreen
	{
		HDC createDc()
		{
			Compat::ScopedCriticalSection lock(g_cs);
			std::unique_ptr<void, decltype(&DeleteObject)> dib(createDib(), DeleteObject);
			if (!dib)
			{
				return nullptr;
			}

			std::unique_ptr<HDC__, decltype(&DeleteDC)> dc(CreateCompatibleDC(nullptr), DeleteDC);
			if (!dc)
			{
				return nullptr;
			}

			HGDIOBJ stockBitmap = SelectObject(dc.get(), dib.get());
			if (!stockBitmap)
			{
				return nullptr;
			}

			dib.release();


			g_stockBitmap = stockBitmap;
			g_dcs.insert(dc.get());
			return dc.release();
		}

		HBITMAP createDib()
		{
			Compat::ScopedCriticalSection lock(g_cs);
			if (!g_surfaceFileMapping)
			{
				return nullptr;
			}
			return createDibSection(g_width, g_height, g_surfaceFileMapping);
		}

		HBITMAP createOffScreenDib(DWORD width, DWORD height)
		{
			Compat::ScopedCriticalSection lock(g_cs);
			return createDibSection(width, height, nullptr);
		}

		CompatPtr<IDirectDrawSurface7> createSurface(const RECT& rect)
		{
			if (rect.left < g_bounds.left || rect.top < g_bounds.top ||
				rect.right > g_bounds.right || rect.bottom > g_bounds.bottom)
			{
				return nullptr;
			}

			Compat::ScopedCriticalSection lock(g_cs);
			DDSURFACEDESC2 desc = {};
			desc.dwSize = sizeof(desc);
			desc.dwFlags = DDSD_WIDTH | DDSD_HEIGHT | DDSD_PIXELFORMAT | DDSD_CAPS | DDSD_PITCH | DDSD_LPSURFACE;
			desc.dwWidth = rect.right - rect.left;
			desc.dwHeight = rect.bottom - rect.top;
			desc.ddpfPixelFormat = DDraw::getRgbPixelFormat(g_bpp);
			desc.ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN | DDSCAPS_SYSTEMMEMORY;
			desc.lPitch = g_pitch;
			desc.lpSurface = static_cast<unsigned char*>(g_surfaceView) +
				(rect.top - g_bounds.top) * g_pitch +
				(rect.left - g_bounds.left) * g_bpp / 8;

			CompatPtr<IDirectDrawSurface7> surface;
			auto dd(DDraw::Repository::getDirectDraw());
			dd->CreateSurface(dd, &desc, &surface.getRef(), nullptr);
			return surface;
		}

		void deleteDc(HDC dc)
		{
			if (!dc)
			{
				return;
			}

			Compat::ScopedCriticalSection lock(g_cs);
			DeleteObject(SelectObject(dc, g_stockBitmap));
			DeleteDC(dc);
			g_dcs.erase(dc);
		}

		RECT getBounds()
		{
			Compat::ScopedCriticalSection lock(g_cs);
			return g_bounds;
		}

		const Region& getRegion()
		{
			Compat::ScopedCriticalSection lock(g_cs);
			return g_region;
		}

		void init()
		{
			InitializeCriticalSection(&g_cs);
			update();
			updatePalette();
		}

		bool update()
		{
			Compat::LogEnter("VirtualScreen::update");
			Compat::ScopedCriticalSection lock(g_cs);

			static auto prevDisplaySettingsUniqueness = Win32::DisplayMode::queryDisplaySettingsUniqueness() - 1;
			const auto currentDisplaySettingsUniqueness = Win32::DisplayMode::queryDisplaySettingsUniqueness();
			if (currentDisplaySettingsUniqueness == prevDisplaySettingsUniqueness)
			{
				Compat::LogLeave("VirtualScreen::update") << false;
				return false;
			}

			prevDisplaySettingsUniqueness = currentDisplaySettingsUniqueness;

			g_region = Region();
			EnumDisplayMonitors(nullptr, nullptr, addMonitorRectToRegion, reinterpret_cast<LPARAM>(&g_region));
			GetRgnBox(g_region, &g_bounds);

			g_bpp = Win32::DisplayMode::getBpp();
			g_width = g_bounds.right - g_bounds.left;
			g_height = g_bounds.bottom - g_bounds.top;
			g_pitch = (g_width * g_bpp / 8 + 3) & ~3;

			if (g_surfaceFileMapping)
			{
				for (HDC dc : g_dcs)
				{
					DeleteObject(SelectObject(dc, g_stockBitmap));
				}
				UnmapViewOfFile(g_surfaceView);
				CloseHandle(g_surfaceFileMapping);
			}

			g_surfaceFileMapping = CreateFileMapping(
				INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, g_pitch * g_height, nullptr);
			g_surfaceView = MapViewOfFile(g_surfaceFileMapping, FILE_MAP_WRITE, 0, 0, 0);

			for (HDC dc : g_dcs)
			{
				SelectObject(dc, createDib());
			}

			Gdi::redraw(nullptr);

			Compat::LogLeave("VirtualScreen::update") << true;
			return true;
		}

		void updatePalette()
		{
			Compat::ScopedCriticalSection lock(g_cs);
			if (8 != g_bpp)
			{
				return;
			}

			PALETTEENTRY pal[256] = {};
			GetSystemPaletteEntries(nullptr, 0, 256, pal);

			RGBQUAD systemPalette[256] = {};
			for (int i = 0; i < 256; ++i)
			{
				systemPalette[i].rgbRed = pal[i].peRed;
				systemPalette[i].rgbGreen = pal[i].peGreen;
				systemPalette[i].rgbBlue = pal[i].peBlue;
			}

			if (0 != memcmp(g_systemPalette, systemPalette, sizeof(systemPalette)))
			{
				memcpy(g_systemPalette, systemPalette, sizeof(systemPalette));
				for (HDC dc : g_dcs)
				{
					SetDIBColorTable(dc, 0, 256, systemPalette);
				}
			}
		}
	}
}
