#include "Common/Hook.h"
#include "Common/Log.h"
#include "D3dDdi/ScopedCriticalSection.h"
#include "Gdi/Gdi.h"
#include "Gdi/ScrollFunctions.h"
#include "Gdi/Window.h"

namespace
{
	BOOL WINAPI scrollWindow(HWND hWnd, int XAmount, int YAmount,
		const RECT* lpRect, const RECT* lpClipRect)
	{
		LOG_FUNC("scrollWindow", hWnd, XAmount, YAmount, lpRect, lpClipRect);
		BOOL result = CALL_ORIG_FUNC(ScrollWindow)(hWnd, XAmount, YAmount, lpRect, lpClipRect);
		if (result)
		{
			Gdi::ScrollFunctions::updateScrolledWindow(hWnd);
		}
		return LOG_RESULT(result);
	}

	int WINAPI scrollWindowEx(HWND hWnd, int dx, int dy, const RECT* prcScroll, const RECT* prcClip,
		HRGN hrgnUpdate, LPRECT prcUpdate, UINT flags)
	{
		LOG_FUNC("scrollWindowEx", hWnd, dx, dy, prcScroll, prcClip, hrgnUpdate, prcUpdate, flags);

		if (flags & SW_SMOOTHSCROLL)
		{
			flags = (LOWORD(flags) & ~SW_SMOOTHSCROLL) | SW_INVALIDATE | SW_ERASE;
		}

		int result = CALL_ORIG_FUNC(ScrollWindowEx)(
			hWnd, dx, dy, prcScroll, prcClip, hrgnUpdate, prcUpdate, flags);
		if (ERROR != result)
		{
			Gdi::ScrollFunctions::updateScrolledWindow(hWnd);
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
		}

		void updateScrolledWindow(HWND hwnd)
		{
			D3dDdi::ScopedCriticalSection lock;
			auto window(Gdi::Window::get(hwnd));
			UINT flags = RDW_ERASE | RDW_INVALIDATE | RDW_NOCHILDREN | RDW_UPDATENOW;
			if (!window || window->getPresentationWindow() != hwnd)
			{
				flags |= RDW_FRAME;
			}
			RedrawWindow(hwnd, nullptr, nullptr, flags);
		}
	}
}
