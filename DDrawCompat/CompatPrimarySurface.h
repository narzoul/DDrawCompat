#pragma once

#define CINTERFACE

#include <ddraw.h>

#include "CompatPtr.h"
#include "CompatRef.h"

class IReleaseNotifier;

namespace CompatPrimarySurface
{
	const DDSURFACEDESC2& getDesc();
	CompatPtr<IDirectDrawSurface7> getPrimary();
	bool isPrimary(void* surface);
	void setPrimary(CompatRef<IDirectDrawSurface7> surface);

	extern CompatWeakPtr<IDirectDrawPalette> palette;
	extern PALETTEENTRY paletteEntries[256];
}
