#include "CompatGdi.h"
#include "CompatGdiDc.h"
#include "CompatGdiPaintHandlers.h"
#include "CompatGdiScrollBar.h"
#include "CompatGdiTitleBar.h"

#include <detours.h>

namespace
{
	LRESULT WINAPI defWindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam,
		WNDPROC origDefWindowProc, const char* funcName);
	LRESULT onNcPaint(HWND hwnd, WPARAM wParam, WNDPROC origWndProc);
	LRESULT onPrint(HWND hwnd, UINT msg, HDC dc, LONG flags, WNDPROC origWndProc);

	LRESULT WINAPI defDlgProcA(HWND hdlg, UINT msg, WPARAM wParam, LPARAM lParam)
	{
		return defWindowProc(hdlg, msg, wParam, lParam, CALL_ORIG_GDI(DefDlgProcA), "defDlgProcA");
	}

	LRESULT WINAPI defDlgProcW(HWND hdlg, UINT msg, WPARAM wParam, LPARAM lParam)
	{
		return defWindowProc(hdlg, msg, wParam, lParam, CALL_ORIG_GDI(DefDlgProcW), "defDlgProcW");
	}

	LRESULT WINAPI defWindowProc(
		HWND hwnd,
		UINT msg,
		WPARAM wParam,
		LPARAM lParam,
		WNDPROC origDefWindowProc,
		const char* funcName)
	{
		Compat::LogEnter(funcName, hwnd, msg, wParam, lParam);
		LRESULT result = 0;

		switch (msg)
		{
		case WM_NCPAINT:
			result = onNcPaint(hwnd, wParam, origDefWindowProc);
			break;

		case WM_PRINT:
		case WM_PRINTCLIENT:
			result = onPrint(hwnd, msg, reinterpret_cast<HDC>(wParam), lParam, origDefWindowProc);
			break;

		default:
			result = origDefWindowProc(hwnd, msg, wParam, lParam);
			break;
		}

		Compat::LogLeave(funcName, hwnd, msg, wParam, lParam) << result;
		return result;
	}

	LRESULT WINAPI defWindowProcA(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
	{
		return defWindowProc(hwnd, msg, wParam, lParam, CALL_ORIG_GDI(DefWindowProcA), "defWindowProcA");
	}

	LRESULT WINAPI defWindowProcW(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
	{
		return defWindowProc(hwnd, msg, wParam, lParam, CALL_ORIG_GDI(DefWindowProcW), "defWindowProcW");
	}

	LRESULT onNcPaint(HWND hwnd, WPARAM wParam, WNDPROC origWndProc)
	{
		if (!hwnd || !CompatGdi::beginGdiRendering())
		{
			return origWndProc(hwnd, WM_NCPAINT, wParam, 0);
		}

		HDC windowDc = GetWindowDC(hwnd);
		HDC compatDc = CompatGdiDc::getDc(windowDc);

		if (compatDc)
		{
			CompatGdi::TitleBar titleBar(hwnd, compatDc);
			titleBar.drawAll();
			titleBar.excludeFromClipRegion();

			CompatGdi::ScrollBar scrollBar(hwnd, compatDc);
			scrollBar.drawAll();
			scrollBar.excludeFromClipRegion();

			SendMessage(hwnd, WM_PRINT, reinterpret_cast<WPARAM>(compatDc), PRF_NONCLIENT);

			CompatGdiDc::releaseDc(windowDc);
		}

		ReleaseDC(hwnd, windowDc);
		CompatGdi::endGdiRendering();
		return 0;
	}

	LRESULT onPrint(HWND hwnd, UINT msg, HDC dc, LONG flags, WNDPROC origWndProc)
	{
		if (!CompatGdi::beginGdiRendering())
		{
			return origWndProc(hwnd, msg, reinterpret_cast<WPARAM>(dc), flags);
		}

		LRESULT result = 0;
		HDC compatDc = CompatGdiDc::getDc(dc);
		if (compatDc)
		{
			result = origWndProc(hwnd, msg, reinterpret_cast<WPARAM>(compatDc), flags);
			CompatGdiDc::releaseDc(dc);
		}
		else
		{
			result = origWndProc(hwnd, msg, reinterpret_cast<WPARAM>(dc), flags);
		}

		CompatGdi::endGdiRendering();
		return result;
	}
}

namespace CompatGdiPaintHandlers
{
	void installHooks()
	{
		DetourTransactionBegin();
		HOOK_GDI_FUNCTION(user32, DefWindowProcA, defWindowProcA);
		HOOK_GDI_FUNCTION(user32, DefWindowProcW, defWindowProcW);
		HOOK_GDI_FUNCTION(user32, DefDlgProcA, defDlgProcA);
		HOOK_GDI_FUNCTION(user32, DefDlgProcW, defDlgProcW);
		DetourTransactionCommit();
	}
}
