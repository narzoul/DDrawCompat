#pragma once

#include <Windows.h>

#include <Common/CompatPtr.h>

namespace Gdi
{
	class Region;

	namespace VirtualScreen
	{
		HDC createDc(bool useDefaultPalette);
		HBITMAP createDib(bool useDefaultPalette);
		HBITMAP createOffScreenDib(LONG width, LONG height, DWORD bpp);
		CompatPtr<IDirectDrawSurface7> createSurface(const RECT& rect);
		void deleteDc(HDC dc);

		RECT getBounds();
		HDC getDc();
		DDSURFACEDESC2 getSurfaceDesc(const RECT& rect);

		void init();
		void setFullscreenMode(bool isFullscreen);
		bool update();
		void updatePalette(PALETTEENTRY(&palette)[256]);
	}
}
