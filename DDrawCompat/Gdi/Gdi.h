#pragma once

#define WIN32_LEAN_AND_MEAN

#include <Windows.h>

namespace Gdi
{
	const ATOM MENU_ATOM = 0x8000;

	typedef void(*WindowPosChangeNotifyFunc)();

	void dllThreadDetach();
	HDC getScreenDc();
	HRGN getVisibleWindowRgn(HWND hwnd);
	void installHooks();
	bool isDisplayDc(HDC dc);
	void redraw(HRGN rgn);
	void redrawWindow(HWND hwnd, HRGN rgn);
	void unhookWndProc(LPCSTR className, WNDPROC oldWndProc);
	void uninstallHooks();
	void watchWindowPosChanges(WindowPosChangeNotifyFunc notifyFunc);
};
