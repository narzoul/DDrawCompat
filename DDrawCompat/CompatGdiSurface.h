#pragma once

class CompatGdiSurface
{
public:
	static void hookGdi();
	static void release();
	static void setSurfaceMemory(void* surfaceMemory, LONG pitch);
};
