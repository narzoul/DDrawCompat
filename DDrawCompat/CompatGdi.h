#pragma once

namespace CompatGdi
{
	class GdiScopedThreadLock
	{
	public:
		GdiScopedThreadLock();
		~GdiScopedThreadLock();
	};

	void installHooks();
	void releaseSurfaceMemory();
	void setSurfaceMemory(void* surfaceMemory, int pitch);
};
