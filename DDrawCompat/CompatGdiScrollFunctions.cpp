#include "CompatGdi.h"
#include "CompatGdiScrollFunctions.h"
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
		BOOL result = CALL_ORIG_FUNC(ScrollWindow)(hWnd, XAmount, YAmount, lpRect, lpClipRect);
		CompatGdiScrollFunctions::updateScrolledWindow(hWnd);
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
		int result = CALL_ORIG_FUNC(ScrollWindowEx)(
			hWnd, dx, dy, prcScroll, prcClip, hrgnUpdate, prcUpdate, flags);
		CompatGdiScrollFunctions::updateScrolledWindow(hWnd);
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
		RedrawWindow(hwnd, nullptr, nullptr, RDW_ERASE | RDW_FRAME | RDW_INVALIDATE | RDW_ALLCHILDREN);
	}
}
