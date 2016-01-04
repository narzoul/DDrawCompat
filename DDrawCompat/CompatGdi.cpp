#include "CompatGdi.h"
#include "CompatGdiDcCache.h"
#include "CompatGdiFunctions.h"

namespace
{
	CRITICAL_SECTION g_gdiCriticalSection;
}

namespace CompatGdi
{
	GdiScopedThreadLock::GdiScopedThreadLock()
	{
		EnterCriticalSection(&g_gdiCriticalSection);
	}

	GdiScopedThreadLock::~GdiScopedThreadLock()
	{
		LeaveCriticalSection(&g_gdiCriticalSection);
	}

	void installHooks()
	{
		InitializeCriticalSection(&g_gdiCriticalSection);
		if (CompatGdiDcCache::init())
		{
			CompatGdiFunctions::hookGdiFunctions();
		}
	}

	void releaseSurfaceMemory()
	{
		GdiScopedThreadLock gdiLock;
		CompatGdiDcCache::release();
	}

	void setSurfaceMemory(void* surfaceMemory, int pitch)
	{
		GdiScopedThreadLock gdiLock;
		const bool wasReleased = CompatGdiDcCache::isReleased();
		CompatGdiDcCache::setSurfaceMemory(surfaceMemory, pitch);
		if (wasReleased)
		{
			InvalidateRect(nullptr, nullptr, TRUE);
		}
	}
}
