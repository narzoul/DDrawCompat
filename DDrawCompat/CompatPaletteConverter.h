#pragma once

#define CINTERFACE

#include <ddraw.h>

namespace CompatPaletteConverter
{
	bool init();
	HDC lockDc();
	IDirectDrawSurface7* lockSurface();
	void release();
	void setClipper(IDirectDrawClipper* clipper);
	void setPalette(IDirectDrawPalette* palette);
	void unlockDc();
	void unlockSurface();
}
