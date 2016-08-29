#include "Common/CompatPtr.h"
#include "Common/Hook.h"
#include "DDraw/DisplayMode.h"
#include "DDraw/Repository.h"
#include "DDrawProcs.h"

namespace
{
	CompatWeakPtr<IDirectDrawSurface7> g_compatibleSurface = {};
	HDC g_compatibleDc = nullptr;
	DDSURFACEDESC2 g_emulatedDisplayMode = {};

	template <typename CStr, typename DevMode,
		typename ChangeDisplaySettingsExPtr, typename EnumDisplaySettingsPtr>
	LONG changeDisplaySettingsEx(
		ChangeDisplaySettingsExPtr origChangeDisplaySettings,
		EnumDisplaySettingsPtr origEnumDisplaySettings,
		CStr lpszDeviceName, DevMode* lpDevMode, HWND hwnd, DWORD dwflags, LPVOID lParam)
	{
		DevMode targetDevMode = {};
		if (lpDevMode)
		{
			targetDevMode = *lpDevMode;
		}
		else
		{
			targetDevMode.dmSize = sizeof(targetDevMode);
			origEnumDisplaySettings(lpszDeviceName, ENUM_REGISTRY_SETTINGS, &targetDevMode);
		}

		if (targetDevMode.dmPelsWidth)
		{
			DevMode currentDevMode = {};
			currentDevMode.dmSize = sizeof(currentDevMode);
			origEnumDisplaySettings(lpszDeviceName, ENUM_CURRENT_SETTINGS, &currentDevMode);

			if (targetDevMode.dmPelsWidth == currentDevMode.dmPelsWidth &&
				targetDevMode.dmPelsHeight == currentDevMode.dmPelsHeight &&
				targetDevMode.dmBitsPerPel == currentDevMode.dmBitsPerPel &&
				targetDevMode.dmDisplayFrequency == currentDevMode.dmDisplayFrequency &&
				targetDevMode.dmDisplayFlags == currentDevMode.dmDisplayFlags)
			{
				HANDLE dwmDxFullScreenTransitionEvent = OpenEventW(
					EVENT_MODIFY_STATE, FALSE, L"DWM_DX_FULLSCREEN_TRANSITION_EVENT");
				SetEvent(dwmDxFullScreenTransitionEvent);
				CloseHandle(dwmDxFullScreenTransitionEvent);
				return DISP_CHANGE_SUCCESSFUL;
			}
		}

		return origChangeDisplaySettings(lpszDeviceName, lpDevMode, hwnd, dwflags, lParam);
	}

	LONG WINAPI changeDisplaySettingsExA(
		LPCSTR lpszDeviceName, DEVMODEA* lpDevMode, HWND hwnd, DWORD dwflags, LPVOID lParam)
	{
		return changeDisplaySettingsEx(CALL_ORIG_FUNC(ChangeDisplaySettingsExA),
			CALL_ORIG_FUNC(EnumDisplaySettingsA), lpszDeviceName, lpDevMode, hwnd, dwflags, lParam);
	}

	LONG WINAPI changeDisplaySettingsExW(
		LPCWSTR lpszDeviceName, DEVMODEW* lpDevMode, HWND hwnd, DWORD dwflags, LPVOID lParam)
	{
		return changeDisplaySettingsEx(CALL_ORIG_FUNC(ChangeDisplaySettingsExW),
			CALL_ORIG_FUNC(EnumDisplaySettingsW), lpszDeviceName, lpDevMode, hwnd, dwflags, lParam);
	}

	CompatPtr<IDirectDrawSurface7> createCompatibleSurface()
	{
		DDSURFACEDESC2 desc = {};
		desc.dwSize = sizeof(desc);
		desc.dwFlags = DDSD_WIDTH | DDSD_HEIGHT | DDSD_PIXELFORMAT | DDSD_CAPS;
		desc.dwWidth = 1;
		desc.dwHeight = 1;
		desc.ddpfPixelFormat = g_emulatedDisplayMode.ddpfPixelFormat;
		desc.ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN | DDSCAPS_SYSTEMMEMORY;

		auto dd = DDraw::Repository::getDirectDraw();
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

	DDSURFACEDESC2 getRealDisplayMode(CompatRef<IDirectDraw7> dd)
	{
		DDSURFACEDESC2 desc = {};
		desc.dwSize = sizeof(desc);
		dd->GetDisplayMode(&dd, &desc);
		return desc;
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

namespace DDraw
{
	namespace DisplayMode
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

		DDSURFACEDESC2 getDisplayMode(CompatRef<IDirectDraw7> dd)
		{
			if (0 == g_emulatedDisplayMode.dwSize)
			{
				g_emulatedDisplayMode = getRealDisplayMode(dd);
			}
			return g_emulatedDisplayMode;
		}

		void installHooks()
		{
			HOOK_FUNCTION(user32, ChangeDisplaySettingsExA, changeDisplaySettingsExA);
			HOOK_FUNCTION(user32, ChangeDisplaySettingsExW, changeDisplaySettingsExW);
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

			g_emulatedDisplayMode = getRealDisplayMode(dd);
			g_emulatedDisplayMode.ddpfPixelFormat = pf;
			updateCompatibleDc();

			return DD_OK;
		}
	}
}
