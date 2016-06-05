#pragma once

#define CINTERFACE

#include <ddraw.h>

#include "CompatRef.h"

namespace CompatDisplayMode
{
	void installHooks();

	HBITMAP WINAPI createCompatibleBitmap(HDC hdc, int cx, int cy);
	HBITMAP WINAPI createDIBitmap(HDC hdc, const BITMAPINFOHEADER* lpbmih, DWORD fdwInit,
		const void* lpbInit, const BITMAPINFO* lpbmi, UINT fuUsage);
	HBITMAP WINAPI createDiscardableBitmap(HDC hdc, int nWidth, int nHeight);

	DDSURFACEDESC2 getDisplayMode(CompatRef<IDirectDraw7> dd);
	HRESULT restoreDisplayMode(CompatRef<IDirectDraw7> dd);
	HRESULT setDisplayMode(CompatRef<IDirectDraw7> dd,
		DWORD width, DWORD height, DWORD bpp, DWORD refreshRate = 0, DWORD flags = 0);
};
