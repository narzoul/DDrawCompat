#pragma once

#include <Gdi/Gdi.h>

namespace Gdi
{
	namespace WinProc
	{
		DWORD adjustComboListBoxRect(HWND hwnd, DWORD awFlags);
		void dllThreadDetach();
		WNDPROC getDDrawOrigWndProc(HWND hwnd);
		void installHooks();
		void onCreateWindow(HWND hwnd);
		void startFrame();
		void updatePresentationWindowText(HWND owner);
	}
}
