#pragma once

#define CINTERFACE

#include <ddraw.h>

#include "CompatWeakPtr.h"

namespace CompatPaletteConverter
{
	bool create();
	HDC getDc();
	CompatWeakPtr<IDirectDrawSurface7> getSurface();
	void release();
	void setClipper(CompatWeakPtr<IDirectDrawClipper> clipper);
	void updatePalette(DWORD startingEntry, DWORD count);
}