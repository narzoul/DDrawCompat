#pragma once

#define CINTERFACE

#include <ddraw.h>

#include "Common/CompatWeakPtr.h"

namespace DDraw
{
	namespace PaletteConverter
	{
		bool create();
		HDC getDc();
		CompatWeakPtr<IDirectDrawSurface7> getSurface();
		void release();
		void setClipper(CompatWeakPtr<IDirectDrawClipper> clipper);
		void updatePalette(DWORD startingEntry, DWORD count);
	}
}
