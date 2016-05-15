#pragma once

#define CINTERFACE

#include <ddraw.h>

#include "CompatPtr.h"
#include "CompatRef.h"

class IReleaseNotifier;

namespace CompatPrimarySurface
{
	struct DisplayMode
	{
		LONG width;
		LONG height;
		DDPIXELFORMAT pixelFormat;
		DWORD refreshRate;
	};

	template <typename TDirectDraw>
	DisplayMode getDisplayMode(TDirectDraw& dd);

	CompatPtr<IDirectDrawSurface7> getPrimary();
	bool isPrimary(void* surface);
	void setPrimary(CompatRef<IDirectDrawSurface7> surface);

	extern DisplayMode displayMode;
	extern bool isDisplayModeChanged;
	extern LPDIRECTDRAWPALETTE palette;
	extern PALETTEENTRY paletteEntries[256];
	extern LONG width;
	extern LONG height;
	extern DDPIXELFORMAT pixelFormat;
	extern IReleaseNotifier releaseNotifier;
}
