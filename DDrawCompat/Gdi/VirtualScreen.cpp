#include <map>

#include <Common/ScopedCriticalSection.h>
#include <Config/Settings/GdiInterops.h>
#include <Config/Settings/SurfacePatches.h>
#include <D3dDdi/Device.h>
#include <D3dDdi/Resource.h>
#include <D3dDdi/ScopedCriticalSection.h>
#include <DDraw/DirectDraw.h>
#include <DDraw/DirectDrawSurface.h>
#include <DDraw/LogUsedResourceFormat.h>
#include <DDraw/RealPrimarySurface.h>
#include <DDraw/ScopedThreadLock.h>
#include <DDraw/Surfaces/PrimarySurface.h>
#include <Gdi/Gdi.h>
#include <Gdi/VirtualScreen.h>
#include <Gdi/Window.h>
#include <Win32/DisplayMode.h>

namespace
{
	struct VirtualScreenDc
	{
		bool useDefaultPalette;
	};

	Compat::CriticalSection g_cs;
	RECT g_bounds = {};
	DWORD g_bpp = 0;
	LONG g_width = 0;
	LONG g_height = 0;
	DWORD g_pitch = 0;
	DWORD g_startOffset = 0;
	HANDLE g_surfaceFileMapping = nullptr;
	void* g_surfaceView = nullptr;
	bool g_isFullscreen = false;

	HGDIOBJ g_stockBitmap = nullptr;
	RGBQUAD g_defaultPalette[256] = {};
	RGBQUAD g_hardwarePalette[256] = {};
	std::map<HDC, VirtualScreenDc> g_dcs;
	HDC g_dc = nullptr;

	RGBQUAD convertToRgbQuad(PALETTEENTRY entry)
	{
		RGBQUAD quad = {};
		quad.rgbRed = entry.peRed;
		quad.rgbGreen = entry.peGreen;
		quad.rgbBlue = entry.peBlue;
		return quad;
	}

	HBITMAP createDibSection(LONG width, LONG height, DWORD bpp, HANDLE section, bool useDefaultPalette)
	{
		struct BITMAPINFO256 : public BITMAPINFO
		{
			RGBQUAD bmiRemainingColors[255];
		};

		BITMAPINFO256 bmi = {};
		bmi.bmiHeader.biSize = sizeof(bmi.bmiHeader);
		bmi.bmiHeader.biWidth = width;
		bmi.bmiHeader.biHeight = height;
		bmi.bmiHeader.biPlanes = 1;
		bmi.bmiHeader.biBitCount = static_cast<WORD>(bpp);
		bmi.bmiHeader.biCompression = 8 == bpp ? BI_RGB : BI_BITFIELDS;

		if (8 == bpp)
		{
			if (useDefaultPalette)
			{
				memcpy(bmi.bmiColors, g_defaultPalette, sizeof(g_defaultPalette));
			}
			else
			{
				memcpy(bmi.bmiColors, g_hardwarePalette, sizeof(g_hardwarePalette));
			}
		}
		else
		{
			const auto pf = DDraw::DirectDraw::getRgbPixelFormat(bpp);
			reinterpret_cast<DWORD&>(bmi.bmiColors[0]) = pf.dwRBitMask;
			reinterpret_cast<DWORD&>(bmi.bmiColors[1]) = pf.dwGBitMask;
			reinterpret_cast<DWORD&>(bmi.bmiColors[2]) = pf.dwBBitMask;
		}

		void* bits = nullptr;
		return CreateDIBSection(nullptr, &bmi, DIB_RGB_COLORS, &bits, section, g_startOffset);
	}
}

namespace Gdi
{
	namespace VirtualScreen
	{
		HDC createDc(bool useDefaultPalette)
		{
			Compat::ScopedCriticalSection lock(g_cs);
			std::unique_ptr<void, decltype(&DeleteObject)> dib(createDib(useDefaultPalette), CALL_ORIG_FUNC(DeleteObject));
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
			g_dcs[dc.get()] = { useDefaultPalette };
			return dc.release();
		}

		HBITMAP createDib(bool useDefaultPalette)
		{
			Compat::ScopedCriticalSection lock(g_cs);
			if (!g_surfaceFileMapping)
			{
				return nullptr;
			}
			return createDibSection(g_width, -g_height, g_bpp, g_surfaceFileMapping, useDefaultPalette);
		}

		HBITMAP createOffScreenDib(LONG width, LONG height, DWORD bpp)
		{
			Compat::ScopedCriticalSection lock(g_cs);
			const bool useDefaultPalette = false;
			return createDibSection(width, height, bpp, nullptr, useDefaultPalette);
		}

		CompatPtr<IDirectDrawSurface7> createSurface(const RECT& rect)
		{
			DDraw::ScopedThreadLock ddLock;
			D3dDdi::ScopedCriticalSection driverLock;
			Compat::ScopedCriticalSection lock(g_cs);

			auto desc = getSurfaceDesc(rect);
			if (!desc.lpSurface)
			{
				return nullptr;
			}

			auto primary(DDraw::PrimarySurface::getPrimary());
			if (!primary)
			{
				return nullptr;
			}

			auto repo = DDraw::DirectDrawSurface::getSurfaceRepository(*DDraw::PrimarySurface::getPrimary());
			if (!repo)
			{
				return nullptr;
			}

			auto dd(repo->getDirectDraw());
			if (!dd)
			{
				return nullptr;
			}

			CompatPtr<IDirectDrawSurface7> surface;
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
			CALL_ORIG_FUNC(DeleteObject)(SelectObject(dc, g_stockBitmap));
			DeleteDC(dc);
			g_dcs.erase(dc);
		}

		RECT getBounds()
		{
			Compat::ScopedCriticalSection lock(g_cs);
			return g_bounds;
		}

		HDC getDc()
		{
			return g_dc;
		}

		DDSURFACEDESC2 getSurfaceDesc(const RECT& rect)
		{
			if (!Config::gdiInterops.anyRedirects())
			{
				return {};
			}

			Compat::ScopedCriticalSection lock(g_cs);
			if (rect.left < g_bounds.left || rect.top < g_bounds.top ||
				rect.right > g_bounds.right || rect.bottom > g_bounds.bottom)
			{
				return {};
			}

			DDSURFACEDESC2 desc = {};
			desc.dwSize = sizeof(desc);
			desc.dwFlags = DDSD_WIDTH | DDSD_HEIGHT | DDSD_PIXELFORMAT | DDSD_CAPS | DDSD_PITCH | DDSD_LPSURFACE;
			desc.dwWidth = rect.right - rect.left;
			desc.dwHeight = rect.bottom - rect.top;
			desc.ddpfPixelFormat = DDraw::DirectDraw::getRgbPixelFormat(g_bpp);
			desc.ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN | DDSCAPS_SYSTEMMEMORY;
			desc.lPitch = g_pitch;
			desc.lpSurface = static_cast<unsigned char*>(g_surfaceView) + g_startOffset +
				(rect.top - g_bounds.top) * g_pitch +
				(rect.left - g_bounds.left) * g_bpp / 8;
			return desc;
		}

		void init()
		{
			PALETTEENTRY entries[20] = {};
			HPALETTE defaultPalette = reinterpret_cast<HPALETTE>(GetStockObject(DEFAULT_PALETTE));
			GetPaletteEntries(defaultPalette, 0, 20, entries);

			for (int i = 0; i < 10; ++i)
			{
				g_defaultPalette[i] = convertToRgbQuad(entries[i]);
				g_defaultPalette[246 + i] = convertToRgbQuad(entries[10 + i]);
			}

			update();
		}

		void setFullscreenMode(bool isFullscreen)
		{
			LOG_FUNC("VirtualScreen::setFullscreenMode", isFullscreen);
			g_isFullscreen = isFullscreen;
			update();
		}

		bool update()
		{
			LOG_FUNC("VirtualScreen::update");
			static auto prevDisplaySettingsUniqueness = Win32::DisplayMode::queryEmulatedDisplaySettingsUniqueness() - 1;
			static bool prevIsFullscreen = false;

			if (prevIsFullscreen == g_isFullscreen)
			{
				Compat::ScopedCriticalSection lock(g_cs);
				if (Win32::DisplayMode::queryEmulatedDisplaySettingsUniqueness() == prevDisplaySettingsUniqueness)
				{
					return LOG_RESULT(false);
				}
			}

			const auto prevBounds = g_bounds;

			{
				D3dDdi::ScopedCriticalSection driverLock;
				Compat::ScopedCriticalSection lock(g_cs);

				prevDisplaySettingsUniqueness = Win32::DisplayMode::queryEmulatedDisplaySettingsUniqueness();
				prevIsFullscreen = g_isFullscreen;

				if (g_isFullscreen)
				{
					g_bounds = DDraw::PrimarySurface::getMonitorInfo().rcEmulated;
				}
				else
				{
					g_bounds = {};
					for (const auto& mi : Win32::DisplayMode::getAllMonitorInfo())
					{
						UnionRect(&g_bounds, &g_bounds, &mi.second.rcMonitor);
					}
				}

				if (!Config::gdiInterops.anyRedirects())
				{
					Gdi::Window::updateFullscreenWindow();
					return LOG_RESULT(true);
				}

				auto gdiResource = D3dDdi::Device::getGdiResource();
				D3dDdi::Device::setGdiResourceHandle(nullptr);

				g_bpp = Win32::DisplayMode::getBpp();
				g_width = g_bounds.right - g_bounds.left;
				g_height = g_bounds.bottom - g_bounds.top;
				g_pitch = (g_width * g_bpp / 8 + 3) & ~3;

				if (g_surfaceFileMapping)
				{
					for (auto& dc : g_dcs)
					{
						CALL_ORIG_FUNC(DeleteObject)(SelectObject(dc.first, g_stockBitmap));
					}
					UnmapViewOfFile(g_surfaceView);
					CloseHandle(g_surfaceFileMapping);
				}

				auto extraRows = Config::surfacePatches.getExtraRows(g_height);
				g_surfaceFileMapping = CreateFileMapping(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0,
					(g_height + extraRows) * g_pitch + DDraw::Surface::ALIGNMENT, nullptr);
				g_surfaceView = MapViewOfFile(g_surfaceFileMapping, FILE_MAP_WRITE, 0, 0, 0);

				auto start = static_cast<BYTE*>(g_surfaceView);
				start += Config::surfacePatches.getTopRows(g_height) * g_pitch;
				start = static_cast<BYTE*>(DDraw::Surface::alignBuffer(start));
				g_startOffset = start - static_cast<BYTE*>(g_surfaceView);

				for (auto& dc : g_dcs)
				{
					SelectObject(dc.first, createDib(dc.second.useDefaultPalette));
				}

				if (gdiResource && DDraw::PrimarySurface::getPrimary() && !DDraw::RealPrimarySurface::isLost())
				{
					D3dDdi::Device::setGdiResourceHandle(*gdiResource);
				}

				if (!g_dc)
				{
					g_dc = createDc(false);
				}
				SetViewportOrgEx(g_dc, -g_bounds.left, -g_bounds.top, nullptr);
			}

			if (g_bounds != prevBounds)
			{
				Gdi::Window::updateAll();
			}
			else
			{
				Gdi::Window::updateFullscreenWindow();
			}
			Gdi::Window::redrawAll();
			return LOG_RESULT(true);
		}

		void updatePalette(PALETTEENTRY(&palette)[256])
		{
			LOG_FUNC("VirtualScreen::updatePalette");
			LOG_DEBUG << Compat::array(palette, 256);
			Compat::ScopedCriticalSection lock(g_cs);

			RGBQUAD hardwarePalette[256] = {};
			for (int i = 0; i < 256; ++i)
			{
				hardwarePalette[i] = convertToRgbQuad(palette[i]);
			}

			if (0 != memcmp(g_hardwarePalette, hardwarePalette, sizeof(hardwarePalette)))
			{
				memcpy(g_hardwarePalette, hardwarePalette, sizeof(hardwarePalette));
				for (auto& dc : g_dcs)
				{
					if (!dc.second.useDefaultPalette)
					{
						SetDIBColorTable(dc.first, 0, 256, hardwarePalette);
					}
				}
			}

			DDraw::RealPrimarySurface::scheduleUpdate();
		}
	}
}
