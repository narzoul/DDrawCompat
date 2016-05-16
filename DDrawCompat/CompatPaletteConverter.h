#pragma once

#define CINTERFACE

#include <ddraw.h>

#include "CompatWeakPtr.h"

namespace CompatPaletteConverter
{
	bool create(const DDSURFACEDESC2& primaryDesc);
	HDC getDc();
	CompatWeakPtr<IDirectDrawSurface7> getSurface();
	void release();
	void setClipper(IDirectDrawClipper* clipper);
	void updatePalette(DWORD startingEntry, DWORD count);
}
