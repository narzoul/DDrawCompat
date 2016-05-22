#include "CompatDisplayMode.h"
#include "CompatPtr.h"
#include "DDrawProcs.h"
#include "DDrawRepository.h"
#include "Hook.h"

namespace
{
	CompatWeakPtr<IDirectDrawSurface7> g_compatibleSurface = {};
	HDC g_compatibleDc = nullptr;
	CompatDisplayMode::DisplayMode g_emulatedDisplayMode = {};

	CompatPtr<IDirectDrawSurface7> createCompatibleSurface()
	{
		DDSURFACEDESC2 desc = {};
		desc.dwSize = sizeof(desc);
		desc.dwFlags = DDSD_WIDTH | DDSD_HEIGHT | DDSD_PIXELFORMAT | DDSD_CAPS;
		desc.dwWidth = 1;
		desc.dwHeight = 1;
		desc.ddpfPixelFormat = g_emulatedDisplayMode.pixelFormat;
		desc.ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN | DDSCAPS_SYSTEMMEMORY;

		auto dd = DDrawRepository::getDirectDraw();
		CompatPtr<IDirectDrawSurface7> surface;
		dd->CreateSurface(dd, &desc, &surface.getRef(), nullptr);
		return surface;
	}

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

	void releaseCompatibleDc()
	{
		if (g_compatibleDc)
		{
			Compat::origProcs.AcquireDDThreadLock();
			g_compatibleSurface->ReleaseDC(g_compatibleSurface, g_compatibleDc);
			g_compatibleDc = nullptr;
			g_compatibleSurface.release();
		}
	}

	void replaceDc(HDC& hdc)
	{
		if (g_compatibleDc && hdc && OBJ_DC == GetObjectType(hdc) &&
			DT_RASDISPLAY == GetDeviceCaps(hdc, TECHNOLOGY))
		{
			hdc = g_compatibleDc;
		}
	}

	void updateCompatibleDc()
	{
		releaseCompatibleDc();
		g_compatibleSurface = createCompatibleSurface().detach();
		if (g_compatibleSurface &&
			SUCCEEDED(g_compatibleSurface->GetDC(g_compatibleSurface, &g_compatibleDc)))
		{
			Compat::origProcs.ReleaseDDThreadLock();
		}
	}
}

namespace CompatDisplayMode
{
	HBITMAP WINAPI createCompatibleBitmap(HDC hdc, int cx, int cy)
	{
		replaceDc(hdc);
		return CALL_ORIG_FUNC(CreateCompatibleBitmap)(hdc, cx, cy);
	}

	HBITMAP WINAPI createDIBitmap(HDC hdc, const BITMAPINFOHEADER* lpbmih, DWORD fdwInit,
		const void* lpbInit, const BITMAPINFO* lpbmi, UINT fuUsage)
	{
		replaceDc(hdc);
		return CALL_ORIG_FUNC(CreateDIBitmap)(hdc, lpbmih, fdwInit, lpbInit, lpbmi, fuUsage);
	}

	HBITMAP WINAPI createDiscardableBitmap(HDC hdc, int nWidth, int nHeight)
	{
		replaceDc(hdc);
		return CALL_ORIG_FUNC(createDiscardableBitmap)(hdc, nWidth, nHeight);
	}

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
			releaseCompatibleDc();
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
		updateCompatibleDc();

		return DD_OK;
	}
}
