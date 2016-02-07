#define WIN32_LEAN_AND_MEAN

#include <dwmapi.h>
#include <Windows.h>

#include "CompatGdi.h"
#include "CompatGdiDc.h"
#include "CompatGdiTitleBar.h"
#include "CompatGdiWinProc.h"
#include "DDrawLog.h"

namespace
{
	void disableDwmAttributes(HWND hwnd);
	void eraseBackground(HWND hwnd, HDC dc);
	bool isScrollBarVisible(HWND hwnd, LONG windowStyles, LONG sbStyle, LONG sbObjectId);
	void ncPaint(HWND hwnd);
	void updateScrolledWindow(HWND hwnd);

	LRESULT CALLBACK callWndRetProc(int nCode, WPARAM wParam, LPARAM lParam)
	{
		if (HC_ACTION == nCode)
		{
			auto ret = reinterpret_cast<CWPRETSTRUCT*>(lParam);
			if (WM_CREATE == ret->message)
			{
				disableDwmAttributes(ret->hwnd);
			}
			else if (WM_ERASEBKGND == ret->message)
			{
				if (0 != ret->lResult)
				{
					eraseBackground(ret->hwnd, reinterpret_cast<HDC>(ret->wParam));
				}
			}
			else if (WM_NCPAINT == ret->message)
			{
				if (0 == ret->lResult)
				{
					ncPaint(ret->hwnd);
				}
			}
			else if (WM_WINDOWPOSCHANGED == ret->message)
			{
				CompatGdi::invalidate();
			}
			else if (WM_VSCROLL == ret->message || WM_HSCROLL == ret->message)
			{
				updateScrolledWindow(ret->hwnd);
			}
		}

		return CallNextHookEx(nullptr, nCode, wParam, lParam);
	}

	void disableDwmAttributes(HWND hwnd)
	{
		DWMNCRENDERINGPOLICY ncRenderingPolicy = DWMNCRP_DISABLED;
		DwmSetWindowAttribute(hwnd, DWMWA_NCRENDERING_POLICY,
			&ncRenderingPolicy, sizeof(ncRenderingPolicy));

		BOOL disableTransitions = TRUE;
		DwmSetWindowAttribute(hwnd, DWMWA_TRANSITIONS_FORCEDISABLED,
			&disableTransitions, sizeof(disableTransitions));
	}

	void drawSizeBox(HWND hwnd, HDC compatDc, const RECT& windowRect, const RECT& clientRect)
	{
		LONG style = GetWindowLongPtr(hwnd, GWL_STYLE);
		if (!(style & WS_SIZEBOX))
		{
			return;
		}

		int width = GetSystemMetrics(SM_CXHSCROLL);
		int height = GetSystemMetrics(SM_CXVSCROLL);
		RECT sizeBoxRect = { 0, 0, width, height };

		const bool isVisibleH = isScrollBarVisible(hwnd, style, WS_HSCROLL, OBJID_HSCROLL);
		const bool isVisibleV = isScrollBarVisible(hwnd, style, WS_VSCROLL, OBJID_VSCROLL);

		OffsetRect(&sizeBoxRect,
			clientRect.right - (!isVisibleH ? width : 0),
			clientRect.bottom - (!isVisibleV ? height : 0));

		if (!isVisibleH || !isVisibleV)
		{
			HRGN sizeBoxRgn = CreateRectRgnIndirect(&sizeBoxRect);
			OffsetRgn(sizeBoxRgn, windowRect.left, windowRect.top);
			ExtSelectClipRgn(compatDc, sizeBoxRgn, RGN_OR);
			DeleteObject(sizeBoxRgn);
		}

		CALL_ORIG_GDI(DrawFrameControl)(compatDc, &sizeBoxRect, DFC_SCROLL, DFCS_SCROLLSIZEGRIP);
	}

	void eraseBackground(HWND hwnd, HDC dc)
	{
		if (CompatGdi::beginGdiRendering())
		{
			HDC compatDc = CompatGdiDc::getDc(dc);
			if (compatDc)
			{
				SendMessage(hwnd, WM_ERASEBKGND, reinterpret_cast<WPARAM>(compatDc), 0);
				CompatGdiDc::releaseDc(dc);
			}
			CompatGdi::endGdiRendering();
		}
	}

	bool isScrollBarVisible(HWND hwnd, LONG windowStyles, LONG sbStyle, LONG sbObjectId)
	{
		if (!(windowStyles & sbStyle))
		{
			return false;
		}

		SCROLLBARINFO sbi = {};
		sbi.cbSize = sizeof(sbi);
		GetScrollBarInfo(hwnd, sbObjectId, &sbi);

		return !(sbi.rgstate[0] & (STATE_SYSTEM_INVISIBLE | STATE_SYSTEM_OFFSCREEN));
	}

	LRESULT CALLBACK mouseProc(int nCode, WPARAM wParam, LPARAM lParam)
	{
		if (HC_ACTION == nCode)
		{
			auto mhs = reinterpret_cast<MOUSEHOOKSTRUCT*>(lParam);
			if (WM_MOUSEWHEEL == wParam)
			{
				updateScrolledWindow(mhs->hwnd);
			}
		}

		return CallNextHookEx(nullptr, nCode, wParam, lParam);
	}

	void ncPaint(HWND hwnd)
	{
		if (!hwnd || !CompatGdi::beginGdiRendering())
		{
			return;
		}

		HDC windowDc = GetWindowDC(hwnd);
		HDC compatDc = CompatGdiDc::getDc(windowDc);

		if (compatDc)
		{
			RECT windowRect = {};
			GetWindowRect(hwnd, &windowRect);
			RECT clientRect = {};
			GetClientRect(hwnd, &clientRect);
			POINT clientOrigin = {};
			ClientToScreen(hwnd, &clientOrigin);

			CompatGdi::TitleBar titleBar(hwnd, compatDc);
			titleBar.drawAll();
			titleBar.excludeFromClipRegion();

			OffsetRect(&clientRect, clientOrigin.x - windowRect.left, clientOrigin.y - windowRect.top);
			ExcludeClipRect(compatDc, clientRect.left, clientRect.top, clientRect.right, clientRect.bottom);
			CALL_ORIG_GDI(BitBlt)(compatDc, 0, 0,
				windowRect.right - windowRect.left, windowRect.bottom - windowRect.top, windowDc, 0, 0, SRCCOPY);

			drawSizeBox(hwnd, compatDc, windowRect, clientRect);

			CompatGdiDc::releaseDc(windowDc);
		}

		ReleaseDC(hwnd, windowDc);
		CompatGdi::endGdiRendering();
	}

	void CALLBACK objectStateChangeEvent(
		HWINEVENTHOOK /*hWinEventHook*/,
		DWORD /*event*/,
		HWND hwnd,
		LONG idObject,
		LONG /*idChild*/,
		DWORD /*dwEventThread*/,
		DWORD /*dwmsEventTime*/)
	{
		if (OBJID_TITLEBAR == idObject)
		{
			if (!hwnd || !CompatGdi::beginGdiRendering())
			{
				return;
			}

			HDC windowDc = GetWindowDC(hwnd);
			HDC compatDc = CompatGdiDc::getDc(windowDc);
			if (compatDc)
			{
				CompatGdi::TitleBar(hwnd, compatDc).drawAll();
				CompatGdiDc::releaseDc(windowDc);
			}

			ReleaseDC(hwnd, windowDc);
			CompatGdi::endGdiRendering();
		}
	}

	void updateScrolledWindow(HWND hwnd)
	{
		RedrawWindow(hwnd, nullptr, nullptr, RDW_ERASE | RDW_FRAME | RDW_INVALIDATE);
	}
}

namespace CompatGdiWinProc
{
	void installHooks()
	{
		const DWORD threadId = GetCurrentThreadId();
		SetWindowsHookEx(WH_CALLWNDPROCRET, callWndRetProc, nullptr, threadId);
		SetWindowsHookEx(WH_MOUSE, &mouseProc, nullptr, threadId);
		SetWinEventHook(EVENT_OBJECT_STATECHANGE, EVENT_OBJECT_STATECHANGE,
			nullptr, &objectStateChangeEvent, 0, threadId, WINEVENT_OUTOFCONTEXT);
	}
}
