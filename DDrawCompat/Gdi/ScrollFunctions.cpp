#include <Common/Hook.h>
#include <Common/Log.h>
#include <Gdi/AccessGuard.h>
#include <Gdi/Dc.h>
#include <Gdi/Gdi.h>
#include <Gdi/Region.h>
#include <Gdi/ScrollBar.h>
#include <Gdi/ScrollFunctions.h>
#include <Gdi/Window.h>

namespace
{
	void scrollWindowDc(HWND hWnd, int dx, int dy, const RECT* prcScroll, const RECT* prcClip)
	{
		HDC dc = GetDC(hWnd);
		ScrollDC(dc, dx, dy, prcScroll, prcClip, nullptr, nullptr);
		CALL_ORIG_FUNC(ReleaseDC)(hWnd, dc);
	}

	BOOL WINAPI scrollWindow(HWND hWnd, int XAmount, int YAmount,
		const RECT* lpRect, const RECT* lpClipRect)
	{
		LOG_FUNC("ScrollWindow", hWnd, XAmount, YAmount, lpRect, lpClipRect);
		BOOL result = CALL_ORIG_FUNC(ScrollWindow)(hWnd, XAmount, YAmount, lpRect, lpClipRect);
		if (result)
		{
			scrollWindowDc(hWnd, XAmount, YAmount, lpRect, lpClipRect);
		}
		return LOG_RESULT(result);
	}

	int WINAPI scrollWindowEx(HWND hWnd, int dx, int dy, const RECT* prcScroll, const RECT* prcClip,
		HRGN hrgnUpdate, LPRECT prcUpdate, UINT flags)
	{
		LOG_FUNC("ScrollWindowEx", hWnd, dx, dy, prcScroll, prcClip, hrgnUpdate, prcUpdate, flags);

		if (flags & SW_SMOOTHSCROLL)
		{
			flags = (LOWORD(flags) & ~SW_SMOOTHSCROLL) | SW_INVALIDATE | SW_ERASE;
		}

		int result = CALL_ORIG_FUNC(ScrollWindowEx)(
			hWnd, dx, dy, prcScroll, prcClip, hrgnUpdate, prcUpdate, flags);
		if (ERROR != result)
		{
			scrollWindowDc(hWnd, dx, dy, prcScroll, prcClip);
		}

		return LOG_RESULT(result);
	}

	int WINAPI setScrollInfo(HWND hwnd, int nBar, LPCSCROLLINFO lpsi, BOOL redraw)
	{
		LOG_FUNC("SetScrollInfo", hwnd, nBar, lpsi, redraw);
		int result = CALL_ORIG_FUNC(SetScrollInfo)(hwnd, nBar, lpsi, redraw);
		if (redraw && (SB_HORZ == nBar || SB_VERT == nBar))
		{
			Gdi::ScrollBar sb(hwnd, nullptr);
			const auto& sbi = SB_HORZ == nBar ? sb.getHorizontalScrollBarInfo() : sb.getVerticalScrollBarInfo();
			if (sbi.isVisible)
			{
				HDC windowDc = GetWindowDC(hwnd);
				SelectClipRgn(windowDc, Gdi::Region(sbi.shaftRect));
				HDC compatDc = Gdi::Dc::getDc(windowDc);
				if (compatDc)
				{
					Gdi::AccessGuard accessGuard(Gdi::ACCESS_WRITE);
					CALL_ORIG_FUNC(DefWindowProcA)(hwnd, WM_PRINT, reinterpret_cast<WPARAM>(compatDc), PRF_NONCLIENT);
					Gdi::Dc::releaseDc(windowDc);
				}
				CALL_ORIG_FUNC(ReleaseDC)(hwnd, windowDc);
			}
		}
		return LOG_RESULT(result);
	}
}

namespace Gdi
{
	namespace ScrollFunctions
	{
		void installHooks()
		{
			HOOK_FUNCTION(user32, ScrollWindow, scrollWindow);
			HOOK_FUNCTION(user32, ScrollWindowEx, scrollWindowEx);
			HOOK_FUNCTION(user32, SetScrollInfo, setScrollInfo);
		}
	}
}
