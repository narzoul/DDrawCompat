#pragma once

#define CINTERFACE

#include <ddraw.h>

#include "CompatRef.h"

namespace CompatDisplayMode
{
	struct DisplayMode
	{
		DWORD width;
		DWORD height;
		DDPIXELFORMAT pixelFormat;
		DWORD refreshRate;
		DWORD flags;
	};

	DisplayMode getDisplayMode(CompatRef<IDirectDraw7> dd);
	DisplayMode getRealDisplayMode(CompatRef<IDirectDraw7> dd);
	HRESULT restoreDisplayMode(CompatRef<IDirectDraw7> dd);
	HRESULT setDisplayMode(CompatRef<IDirectDraw7> dd,
		DWORD width, DWORD height, DWORD bpp, DWORD refreshRate = 0, DWORD flags = 0);
};
