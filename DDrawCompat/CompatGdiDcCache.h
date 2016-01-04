#pragma once

#define CINTERFACE
#define WIN32_LEAN_AND_MEAN

#include <ddraw.h>
#include <Windows.h>

namespace CompatGdiDcCache
{
	struct SurfaceMemoryDesc
	{
		void* surfaceMemory;
		LONG pitch;
	};

	struct CachedDc
	{
		SurfaceMemoryDesc surfaceMemoryDesc;
		IDirectDrawSurface7* surface;
		HDC dc;
	};

	CachedDc getDc();
	bool init();
	bool isReleased();
	void release();
	void releaseDc(const CachedDc& cachedDc);
	void setSurfaceMemory(void* surfaceMemory, LONG pitch);
}
