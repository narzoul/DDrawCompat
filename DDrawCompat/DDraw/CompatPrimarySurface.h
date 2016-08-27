#pragma once

#define CINTERFACE

#include <ddraw.h>

#include "CompatPtr.h"
#include "CompatRef.h"

namespace DDraw
{
	namespace CompatPrimarySurface
	{
		const DDSURFACEDESC2& getDesc();
		CompatPtr<IDirectDrawSurface7> getPrimary();
		bool isPrimary(void* surface);
		void setPrimary(CompatRef<IDirectDrawSurface7> surface);

		extern CompatWeakPtr<IDirectDrawPalette> g_palette;
		extern PALETTEENTRY g_paletteEntries[256];
	}
}
