#pragma once

#define WIN32_LEAN_AND_MEAN

#include <Windows.h>

#include "CompatGdiDcCache.h"

namespace CompatGdiDc
{
	using CompatGdiDcCache::CachedDc;

	CachedDc getDc(HDC origDc);
	void releaseDc(const CachedDc& cachedDc);
}
