#include "DDraw/Surfaces/PrimarySurface.h"
#include "Gdi/Caret.h"
#include "Gdi/DcFunctions.h"
#include "Gdi/Gdi.h"
#include "Gdi/PaintHandlers.h"
#include "Gdi/ScrollFunctions.h"
#include "Gdi/Window.h"
#include "Gdi/WinProc.h"

namespace
{
	DWORD g_gdiThreadId = 0;
	HDC g_screenDc = nullptr;

	BOOL CALLBACK redrawWindowCallback(HWND hwnd, LPARAM lParam)
	{
		Gdi::redrawWindow(hwnd, reinterpret_cast<HRGN>(lParam));
		return TRUE;
	}
}

namespace Gdi
{
	DWORD getGdiThreadId()
	{
		return g_gdiThreadId;
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
		g_gdiThreadId = GetCurrentThreadId();
		g_screenDc = GetDC(nullptr);

		Gdi::DcFunctions::installHooks();
		Gdi::PaintHandlers::installHooks();
		Gdi::ScrollFunctions::installHooks();
		Gdi::WinProc::installHooks();
		Gdi::Caret::installHooks();
	}

	void redraw(HRGN rgn)
	{
		EnumThreadWindows(g_gdiThreadId, &redrawWindowCallback, reinterpret_cast<LPARAM>(rgn));
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
		RedrawWindow(hwnd, nullptr, rgn, RDW_ERASENOW);

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
		Gdi::Caret::uninstallHooks();
		Gdi::WinProc::uninstallHooks();
		Gdi::PaintHandlers::uninstallHooks();
		ReleaseDC(nullptr, g_screenDc);
	}

	void watchWindowPosChanges(WindowPosChangeNotifyFunc notifyFunc)
	{
		WinProc::watchWindowPosChanges(notifyFunc);
	}
}
