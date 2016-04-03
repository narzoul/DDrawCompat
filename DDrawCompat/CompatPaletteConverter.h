#pragma once

#define CINTERFACE

#include <ddraw.h>

namespace CompatPaletteConverter
{
	bool create();
	void init();
	HDC lockDc();
	IDirectDrawSurface7* lockSurface();
	void release();
	void setClipper(IDirectDrawClipper* clipper);
	void setHalftonePalette();
	void setPrimaryPalette(DWORD startingEntry, DWORD count);
	void unlockDc();
	void unlockSurface();
}
