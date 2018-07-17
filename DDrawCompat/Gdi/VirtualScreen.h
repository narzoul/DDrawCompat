#pragma once

#define WIN32_LEAN_AND_MEAN

#include <Windows.h>

#include "Common/CompatPtr.h"

namespace Gdi
{
	class Region;

	namespace VirtualScreen
	{
		HDC createDc();
		HBITMAP createDib();
		CompatPtr<IDirectDrawSurface7> createSurface(const RECT& rect);
		void deleteDc(HDC dc);

		RECT getBounds();
		const Region& getRegion();

		void init();
		bool update();
		void updatePalette();
	}
}
