#include "CompatDisplayMode.h"

namespace
{
	CompatDisplayMode::DisplayMode g_emulatedDisplayMode = {};

	template <typename TSurfaceDesc>
	HRESULT PASCAL enumDisplayModesCallback(
		TSurfaceDesc* lpDDSurfaceDesc,
		LPVOID lpContext)
	{
		if (lpDDSurfaceDesc)
		{
			*static_cast<DDPIXELFORMAT*>(lpContext) = lpDDSurfaceDesc->ddpfPixelFormat;
		}
		return DDENUMRET_CANCEL;
	}

	DDPIXELFORMAT getDisplayModePixelFormat(
		CompatRef<IDirectDraw7> dd, DWORD width, DWORD height, DWORD bpp)
	{
		DDSURFACEDESC2 desc = {};
		desc.ddpfPixelFormat.dwSize = sizeof(desc.ddpfPixelFormat);
		desc.ddpfPixelFormat.dwFlags = DDPF_RGB;
		desc.ddpfPixelFormat.dwRGBBitCount = bpp;

		if (bpp <= 8)
		{
			switch (bpp)
			{
			case 1: desc.ddpfPixelFormat.dwFlags |= DDPF_PALETTEINDEXED1; break;
			case 2: desc.ddpfPixelFormat.dwFlags |= DDPF_PALETTEINDEXED2; break;
			case 4: desc.ddpfPixelFormat.dwFlags |= DDPF_PALETTEINDEXED4; break;
			case 8: desc.ddpfPixelFormat.dwFlags |= DDPF_PALETTEINDEXED8; break;
			default: return DDPIXELFORMAT();
			}
			return desc.ddpfPixelFormat;
		}

		desc.dwSize = sizeof(desc);
		desc.dwFlags = DDSD_WIDTH | DDSD_HEIGHT | DDSD_PIXELFORMAT;
		desc.dwWidth = width;
		desc.dwHeight = height;

		DDPIXELFORMAT pf = {};
		if (FAILED(dd->EnumDisplayModes(&dd, 0, &desc, &pf, &enumDisplayModesCallback)) ||
			0 == pf.dwSize)
		{
			Compat::Log() << "Failed to find the requested display mode: " <<
				width << "x" << height << "x" << bpp;
		}
		
		return pf;
	}
}

namespace CompatDisplayMode
{
	DisplayMode getDisplayMode(CompatRef<IDirectDraw7> dd)
	{
		if (0 == g_emulatedDisplayMode.width)
		{
			g_emulatedDisplayMode = getRealDisplayMode(dd);
		}
		return g_emulatedDisplayMode;
	}

	DisplayMode getRealDisplayMode(CompatRef<IDirectDraw7> dd)
	{
		DDSURFACEDESC2 desc = {};
		desc.dwSize = sizeof(desc);
		dd->GetDisplayMode(&dd, &desc);

		DisplayMode dm = {};
		dm.width = desc.dwWidth;
		dm.height = desc.dwHeight;
		dm.pixelFormat = desc.ddpfPixelFormat;
		dm.refreshRate = desc.dwRefreshRate;
		return dm;
	}

	HRESULT restoreDisplayMode(CompatRef<IDirectDraw7> dd)
	{
		const HRESULT result = dd->RestoreDisplayMode(&dd);
		if (SUCCEEDED(result))
		{
			ZeroMemory(&g_emulatedDisplayMode, sizeof(g_emulatedDisplayMode));
		}
		return result;
	}

	HRESULT setDisplayMode(CompatRef<IDirectDraw7> dd,
		DWORD width, DWORD height, DWORD bpp, DWORD refreshRate, DWORD flags)
	{
		DDPIXELFORMAT pf = getDisplayModePixelFormat(dd, width, height, bpp);
		if (0 == pf.dwSize)
		{
			return DDERR_INVALIDMODE;
		}

		const HRESULT result = dd->SetDisplayMode(&dd, width, height, 32, refreshRate, flags);
		if (FAILED(result))
		{
			Compat::Log() << "Failed to set the display mode to " << width << "x" << height <<
				"x" << bpp << " (" << std::hex << result << std::dec << ')';
			return result;
		}

		g_emulatedDisplayMode.width = width;
		g_emulatedDisplayMode.height = height;
		g_emulatedDisplayMode.pixelFormat = pf;
		g_emulatedDisplayMode.refreshRate = refreshRate;
		g_emulatedDisplayMode.flags = flags;

		return DD_OK;
	}
}
