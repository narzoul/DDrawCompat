#pragma once

typedef unsigned long DWORD;

namespace Config
{
	const DWORD gdiDcCacheSize = 10;
	const DWORD minRefreshInterval = 1000 / 60;
	const DWORD minRefreshIntervalAfterFlip = 1000 / 10;
	const DWORD minPaletteUpdateInterval = 1000 / 60;
}
