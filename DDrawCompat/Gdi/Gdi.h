#pragma once

#include <Windows.h>

namespace Gdi
{
	const ATOM MENU_ATOM = 0x8000;

	typedef void(*WindowPosChangeNotifyFunc)();

	void dllThreadDetach();
	HRGN getVisibleWindowRgn(HWND hwnd);
	void installHooks();
	bool isDisplayDc(HDC dc);
	void redraw(HRGN rgn);
	void redrawWindow(HWND hwnd, HRGN rgn);
	void unhookWndProc(LPCSTR className, WNDPROC oldWndProc);
	void uninstallHooks();
	void watchWindowPosChanges(WindowPosChangeNotifyFunc notifyFunc);
};
