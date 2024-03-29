#include <dwmapi.h>

#include <Common/Hook.h>
#include <DDraw/Surfaces/PrimarySurface.h>
#include <Gdi/Caret.h>
#include <Gdi/Cursor.h>
#include <Gdi/Dc.h>
#include <Gdi/DcFunctions.h>
#include <Gdi/Font.h>
#include <Gdi/Gdi.h>
#include <Gdi/GuiThread.h>
#include <Gdi/Icon.h>
#include <Gdi/Metrics.h>
#include <Gdi/Palette.h>
#include <Gdi/PresentationWindow.h>
#include <Gdi/ScrollFunctions.h>
#include <Gdi/User32WndProcs.h>
#include <Gdi/WinProc.h>

namespace
{
	HRESULT WINAPI dwmEnableComposition([[maybe_unused]] UINT uCompositionAction)
	{
		LOG_FUNC("DwmEnableComposition", uCompositionAction);
		return LOG_RESULT(0);
	}

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
	void checkDesktopComposition()
	{
		BOOL isEnabled = FALSE;
		HRESULT result = DwmIsCompositionEnabled(&isEnabled);
		LOG_DEBUG << "DwmIsCompositionEnabled: " << Compat::hex(result) << " " << isEnabled;
		if (!isEnabled)
		{
			LOG_ONCE("Warning: Desktop composition is disabled. This is not supported.");
		}
	}

	void dllThreadDetach()
	{
		WinProc::dllThreadDetach();
		Dc::dllThreadDetach();
	}

	void installHooks()
	{
#pragma warning (disable : 4995)
		HOOK_FUNCTION(dwmapi, DwmEnableComposition, dwmEnableComposition);
#pragma warning (default : 4995)

		checkDesktopComposition();
		DisableProcessWindowsGhosting();

		DcFunctions::installHooks();
		Icon::installHooks();
		Metrics::installHooks();
		Palette::installHooks();
		PresentationWindow::installHooks();
		ScrollFunctions::installHooks();
		User32WndProcs::installHooks();
		Caret::installHooks();
		Cursor::installHooks();
		Font::installHooks();
		WinProc::installHooks();
		GuiThread::installHooks();
	}

	bool isDisplayDc(HDC dc)
	{
		return dc && OBJ_DC == GetObjectType(dc) && DT_RASDISPLAY == CALL_ORIG_FUNC(GetDeviceCaps)(dc, TECHNOLOGY) &&
			!(CALL_ORIG_FUNC(GetWindowLongA)(CALL_ORIG_FUNC(WindowFromDC)(dc), GWL_EXSTYLE) & WS_EX_LAYERED) &&
			MENU_ATOM != GetClassLong(CALL_ORIG_FUNC(WindowFromDC)(dc), GCW_ATOM);
	}

	void redraw(HRGN rgn)
	{
		CALL_ORIG_FUNC(EnumWindows)(&redrawWindowCallback, reinterpret_cast<LPARAM>(rgn));
	}

	void redrawWindow(HWND hwnd, HRGN rgn)
	{
		if (!IsWindowVisible(hwnd) || IsIconic(hwnd) || GuiThread::isGuiThreadWindow(hwnd))
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

	void watchWindowPosChanges(WindowPosChangeNotifyFunc notifyFunc)
	{
		WinProc::watchWindowPosChanges(notifyFunc);
	}
}
