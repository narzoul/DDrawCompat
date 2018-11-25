#include "DDraw/Surfaces/PrimarySurface.h"
#include "Gdi/Caret.h"
#include "Gdi/Dc.h"
#include "Gdi/DcCache.h"
#include "Gdi/DcFunctions.h"
#include "Gdi/Gdi.h"
#include "Gdi/PaintHandlers.h"
#include "Gdi/ScrollFunctions.h"
#include "Gdi/Window.h"
#include "Gdi/WinProc.h"

namespace
{
	HDC g_screenDc = nullptr;

	BOOL CALLBACK redrawWindowCallback(HWND hwnd, LPARAM lParam)
	{
		DWORD windowPid = 0;
		GetWindowThreadProcessId(hwnd, &windowPid);
		if (GetCurrentProcessId() == windowPid)
		{
			Gdi::redrawWindow(hwnd, reinterpret_cast<HRGN>(lParam));
		}
		return TRUE;
	}
}

namespace Gdi
{
	void dllThreadDetach()
	{
		WinProc::dllThreadDetach();
		Dc::dllThreadDetach();
		DcCache::dllThreadDetach();
	}

	HDC getScreenDc()
	{
		return g_screenDc;
	}

	HRGN getVisibleWindowRgn(HWND hwnd)
	{
		return DcFunctions::getVisibleWindowRgn(hwnd);
	}

	void installHooks()
	{
		g_screenDc = GetDC(nullptr);

		DcFunctions::installHooks();
		PaintHandlers::installHooks();
		ScrollFunctions::installHooks();
		Window::installHooks();
		WinProc::installHooks();
		Caret::installHooks();
	}

	void redraw(HRGN rgn)
	{
		EnumWindows(&redrawWindowCallback, reinterpret_cast<LPARAM>(rgn));
	}

	void redrawWindow(HWND hwnd, HRGN rgn)
	{
		if (!IsWindowVisible(hwnd) || IsIconic(hwnd) || Window::isPresentationWindow(hwnd))
		{
			return;
		}

		POINT origin = {};
		if (rgn)
		{
			ClientToScreen(hwnd, &origin);
			OffsetRgn(rgn, -origin.x, -origin.y);
		}

		RedrawWindow(hwnd, nullptr, rgn, RDW_ERASE | RDW_FRAME | RDW_INVALIDATE | RDW_ALLCHILDREN);

		if (rgn)
		{
			OffsetRgn(rgn, origin.x, origin.y);
		}
	}

	void unhookWndProc(LPCSTR className, WNDPROC oldWndProc)
	{
		HWND hwnd = CreateWindow(className, nullptr, 0, 0, 0, 0, 0, nullptr, nullptr, nullptr, 0);
		SetClassLongPtr(hwnd, GCLP_WNDPROC, reinterpret_cast<LONG>(oldWndProc));
		DestroyWindow(hwnd);
	}

	void uninstallHooks()
	{
		Caret::uninstallHooks();
		WinProc::uninstallHooks();
		Window::uninstallHooks();
		Dc::dllProcessDetach();
		DcCache::dllProcessDetach();
		ReleaseDC(nullptr, g_screenDc);
	}

	void watchWindowPosChanges(WindowPosChangeNotifyFunc notifyFunc)
	{
		WinProc::watchWindowPosChanges(notifyFunc);
	}
}
