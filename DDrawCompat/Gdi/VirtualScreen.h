#pragma once

#include <Windows.h>

#include "Common/CompatPtr.h"

namespace Gdi
{
	class Region;

	namespace VirtualScreen
	{
		HDC createDc();
		HBITMAP createDib();
		HBITMAP createOffScreenDib(LONG width, LONG height);
		CompatPtr<IDirectDrawSurface7> createSurface(const RECT& rect);
		void deleteDc(HDC dc);

		RECT getBounds();
		Region getRegion();
		DDSURFACEDESC2 getSurfaceDesc(const RECT& rect);

		void init();
		bool update();
		void updatePalette(PALETTEENTRY(&palette)[256]);
	}
}
