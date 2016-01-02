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

	struct CompatDc
	{
		DWORD cacheId;
		SurfaceMemoryDesc surfaceMemoryDesc;
		IDirectDrawSurface7* surface;
		HDC origDc;
		HDC dc;
		int dcState;
	};

	CompatDc getDc();
	bool init();
	bool isReleased();
	void release();
	void returnDc(const CompatDc& compatDc);
	void setSurfaceMemory(void* surfaceMemory, LONG pitch);
}
