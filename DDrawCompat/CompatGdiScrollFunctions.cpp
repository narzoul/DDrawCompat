#include "CompatGdi.h"
#include "CompatGdiScrollFunctions.h"
#include "DDrawLog.h"
#include "Hook.h"

namespace
{
	BOOL WINAPI scrollWindow(
		_In_       HWND hWnd,
		_In_       int  XAmount,
		_In_       int  YAmount,
		_In_ const RECT *lpRect,
		_In_ const RECT *lpClipRect)
	{
		Compat::LogEnter("scrollWindow", hWnd, XAmount, YAmount, lpRect, lpClipRect);
		BOOL result = CALL_ORIG_FUNC(ScrollWindow)(hWnd, XAmount, YAmount, lpRect, lpClipRect);
		if (result)
		{
			CompatGdiScrollFunctions::updateScrolledWindow(hWnd);
		}
		Compat::LogLeave("scrollWindow", hWnd, XAmount, YAmount, lpRect, lpClipRect) << result;
		return result;
	}

	int WINAPI scrollWindowEx(
		_In_        HWND   hWnd,
		_In_        int    dx,
		_In_        int    dy,
		_In_  const RECT   *prcScroll,
		_In_  const RECT   *prcClip,
		_In_        HRGN   hrgnUpdate,
		_Out_       LPRECT prcUpdate,
		_In_        UINT   flags)
	{
		Compat::LogEnter("scrollWindowEx",
			hWnd, dx, dy, prcScroll, prcClip, hrgnUpdate, prcUpdate, flags);
		int result = CALL_ORIG_FUNC(ScrollWindowEx)(
			hWnd, dx, dy, prcScroll, prcClip, hrgnUpdate, prcUpdate, flags);
		if (ERROR != result)
		{
			CompatGdiScrollFunctions::updateScrolledWindow(hWnd);
		}
		Compat::LogLeave("scrollWindowEx",
			hWnd, dx, dy, prcScroll, prcClip, hrgnUpdate, prcUpdate, flags) << result;
		return result;
	}
}

namespace CompatGdiScrollFunctions
{
	void installHooks()
	{
		Compat::beginHookTransaction();
		HOOK_FUNCTION(user32, ScrollWindow, scrollWindow);
		HOOK_FUNCTION(user32, ScrollWindowEx, scrollWindowEx);
		Compat::endHookTransaction();
	}

	void updateScrolledWindow(HWND hwnd)
	{
		RedrawWindow(hwnd, nullptr, nullptr, RDW_ERASE | RDW_FRAME | RDW_INVALIDATE | RDW_NOCHILDREN);
	}
}
