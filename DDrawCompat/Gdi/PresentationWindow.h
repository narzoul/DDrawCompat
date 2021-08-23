#pragma once

#include <Windows.h>

namespace Overlay
{
	class ConfigWindow;
}

namespace Gdi
{
	namespace PresentationWindow
	{
		HWND create(HWND owner, WNDPROC wndProc = nullptr);
		void destroy(HWND hwnd);
		Overlay::ConfigWindow* getConfigWindow();
		bool isPresentationWindow(HWND hwnd);
		bool isThreadReady();
		void setWindowPos(HWND hwnd, const WINDOWPOS& wp);
		void setWindowRgn(HWND hwnd, HRGN rgn);
		void startThread();

		void installHooks();
	}
}
