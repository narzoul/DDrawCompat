#pragma once

#define CINTERFACE

#include <ddraw.h>

#include "CompatWeakPtr.h"
#include "CompatRef.h"

class RealPrimarySurface
{
public:
	template <typename DirectDraw>
	static HRESULT create(CompatRef<DirectDraw> dd);

	static void disableUpdates();
	static void enableUpdates();
	static HRESULT flip(DWORD flags);
	static CompatWeakPtr<IDirectDrawSurface7> getSurface();
	static void invalidate(const RECT* rect);
	static bool isFullScreen();
	static bool isLost();
	static void release();
	static void removeUpdateThread();
	static HRESULT restore();
	static void setClipper(LPDIRECTDRAWCLIPPER clipper);
	static void setPalette();
	static void update();
	static void updatePalette(DWORD startingEntry, DWORD count);
};
