#define WIN32_LEAN_AND_MEAN

#include <Windows.h>

#include "CompatGdi.h"
#include "CompatGdiDc.h"
#include "CompatGdiWinProc.h"
#include "DDrawLog.h"

namespace
{
	void eraseBackground(HWND hwnd, HDC dc);
	void ncPaint(HWND hwnd);
	void updateScrolledWindow(HWND hwnd);

	LRESULT CALLBACK callWndRetProc(int nCode, WPARAM wParam, LPARAM lParam)
	{
		if (HC_ACTION == nCode)
		{
			auto ret = reinterpret_cast<CWPRETSTRUCT*>(lParam);
			if (WM_ERASEBKGND == ret->message)
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
		if (!CompatGdi::beginGdiRendering())
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

			OffsetRect(&clientRect, clientOrigin.x - windowRect.left, clientOrigin.y - windowRect.top);
			ExcludeClipRect(compatDc, clientRect.left, clientRect.top, clientRect.right, clientRect.bottom);
			CALL_ORIG_GDI(BitBlt)(compatDc, 0, 0,
				windowRect.right - windowRect.left, windowRect.bottom - windowRect.top, windowDc, 0, 0, SRCCOPY);

			CompatGdiDc::releaseDc(windowDc);
		}

		ReleaseDC(hwnd, windowDc);
		CompatGdi::endGdiRendering();
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
	}
}
