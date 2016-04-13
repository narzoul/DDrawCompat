#pragma once

#define WIN32_LEAN_AND_MEAN

#include <Windows.h>

namespace CompatGdi
{
	bool beginGdiRendering();
	void endGdiRendering();

	void hookWndProc(LPCSTR className, WNDPROC &oldWndProc, WNDPROC newWndProc);
	void installHooks();
	void invalidate(const RECT* rect);
	void updatePalette(DWORD startingEntry, DWORD count);

	extern CRITICAL_SECTION g_gdiCriticalSection;
};
