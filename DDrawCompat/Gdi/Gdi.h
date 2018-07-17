#pragma once

#define WIN32_LEAN_AND_MEAN

#include <Windows.h>

namespace Gdi
{
	typedef void(*WindowPosChangeNotifyFunc)();

	DWORD getGdiThreadId();
	HDC getScreenDc();
	HRGN getVisibleWindowRgn(HWND hwnd);
	void hookWndProc(LPCSTR className, WNDPROC &oldWndProc, WNDPROC newWndProc);
	void installHooks();
	void redraw(HRGN rgn);
	void redrawWindow(HWND hwnd, HRGN rgn);
	void unhookWndProc(LPCSTR className, WNDPROC oldWndProc);
	void uninstallHooks();
	void watchWindowPosChanges(WindowPosChangeNotifyFunc notifyFunc);
};
