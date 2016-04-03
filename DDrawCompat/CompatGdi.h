#pragma once

#define WIN32_LEAN_AND_MEAN

#include <Windows.h>

namespace CompatGdi
{
	class GdiScopedThreadLock
	{
	public:
		GdiScopedThreadLock();
		~GdiScopedThreadLock();
		void unlock();
	private:
		bool m_isLocked;
	};

	bool beginGdiRendering();
	void endGdiRendering();

	void hookWndProc(LPCSTR className, WNDPROC &oldWndProc, WNDPROC newWndProc);
	void installHooks();
	void invalidate(const RECT* rect);
	void updatePalette(DWORD startingEntry, DWORD count);
};
