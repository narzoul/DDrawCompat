#define WIN32_LEAN_AND_MEAN

#include <unordered_map>

#include <dwmapi.h>
#include <Windows.h>

#include "CompatGdi.h"
#include "CompatGdiDc.h"
#include "CompatGdiScrollBar.h"
#include "CompatGdiScrollFunctions.h"
#include "CompatGdiTitleBar.h"
#include "CompatGdiWinProc.h"
#include "DDrawLog.h"

namespace
{
	std::unordered_map<HWND, RECT> g_prevWindowRect;

	void disableDwmAttributes(HWND hwnd);
	void eraseBackground(HWND hwnd, HDC dc);
	void ncPaint(HWND hwnd);
	void onWindowPosChanged(HWND hwnd);
	void removeDropShadow(HWND hwnd);

	LRESULT CALLBACK callWndRetProc(int nCode, WPARAM wParam, LPARAM lParam)
	{
		auto ret = reinterpret_cast<CWPRETSTRUCT*>(lParam);
		Compat::LogEnter("callWndRetProc", nCode, wParam, ret);

		if (HC_ACTION == nCode)
		{
			if (WM_CREATE == ret->message)
			{
				disableDwmAttributes(ret->hwnd);
				removeDropShadow(ret->hwnd);
			}
			else if (WM_DESTROY == ret->message)
			{
				CompatGdi::GdiScopedThreadLock lock;
				g_prevWindowRect.erase(ret->hwnd);
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
				onWindowPosChanged(ret->hwnd);
			}
			else if (WM_VSCROLL == ret->message || WM_HSCROLL == ret->message)
			{
				CompatGdiScrollFunctions::updateScrolledWindow(ret->hwnd);
			}
			else if (WM_COMMAND == ret->message)
			{
				auto msgSource = LOWORD(ret->wParam);
				auto notifCode = HIWORD(ret->wParam);
				if (0 != msgSource && 1 != msgSource && (EN_HSCROLL == notifCode || EN_VSCROLL == notifCode))
				{
					CompatGdiScrollFunctions::updateScrolledWindow(reinterpret_cast<HWND>(ret->lParam));
				}
			}
			else if (BM_SETSTYLE == ret->message)
			{
				RedrawWindow(ret->hwnd, nullptr, nullptr, RDW_ERASE | RDW_FRAME | RDW_INVALIDATE);
			}
		}

		LRESULT result = CallNextHookEx(nullptr, nCode, wParam, lParam);
		Compat::LogLeave("callWndRetProc", nCode, wParam, ret) << result;
		return result;
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

	bool drawButtonHighlight(HWND hwnd, const RECT& windowRect, HDC compatDc)
	{
		if (SendMessage(hwnd, WM_GETDLGCODE, 0, 0) & DLGC_DEFPUSHBUTTON)
		{
			RECT rect = {};
			SetRect(&rect, 0, 0, windowRect.right - windowRect.left, windowRect.bottom - windowRect.top);
			FrameRect(compatDc, &rect, GetSysColorBrush(COLOR_WINDOWFRAME));
			return true;
		}

		return false;
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
			if (WM_MOUSEWHEEL == wParam || WM_MOUSEHWHEEL == wParam)
			{
				CompatGdiScrollFunctions::updateScrolledWindow(mhs->hwnd);
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

			if (!drawButtonHighlight(hwnd, windowRect, compatDc))
			{
				CompatGdi::TitleBar titleBar(hwnd, compatDc);
				titleBar.drawAll();
				titleBar.excludeFromClipRegion();

				CompatGdi::ScrollBar scrollBar(hwnd, compatDc);
				scrollBar.drawAll();
				scrollBar.excludeFromClipRegion();

				RECT clientRect = {};
				GetClientRect(hwnd, &clientRect);
				POINT clientOrigin = {};
				ClientToScreen(hwnd, &clientOrigin);

				OffsetRect(&clientRect, clientOrigin.x - windowRect.left, clientOrigin.y - windowRect.top);
				ExcludeClipRect(compatDc, clientRect.left, clientRect.top, clientRect.right, clientRect.bottom);
				CALL_ORIG_GDI(BitBlt)(compatDc, 0, 0,
					windowRect.right - windowRect.left, windowRect.bottom - windowRect.top, windowDc, 0, 0, SRCCOPY);
			}

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
		if (OBJID_TITLEBAR == idObject || OBJID_HSCROLL == idObject || OBJID_VSCROLL == idObject)
		{
			if (!hwnd || !CompatGdi::beginGdiRendering())
			{
				return;
			}

			HDC windowDc = GetWindowDC(hwnd);
			HDC compatDc = CompatGdiDc::getDc(windowDc);
			if (compatDc)
			{
				if (OBJID_TITLEBAR == idObject)
				{
					CompatGdi::TitleBar(hwnd, compatDc).drawAll();
				}
				else if (OBJID_HSCROLL == idObject)
				{
					CompatGdi::ScrollBar(hwnd, compatDc).drawHorizArrows();
				}
				else if (OBJID_VSCROLL == idObject)
				{
					CompatGdi::ScrollBar(hwnd, compatDc).drawVertArrows();
				}
				CompatGdiDc::releaseDc(windowDc);
			}

			ReleaseDC(hwnd, windowDc);
			CompatGdi::endGdiRendering();
		}
	}

	void onWindowPosChanged(HWND hwnd)
	{
		CompatGdi::GdiScopedThreadLock lock;

		const auto it = g_prevWindowRect.find(hwnd);
		if (it != g_prevWindowRect.end())
		{
			CompatGdi::invalidate(&it->second);
		}

		if (IsWindowVisible(hwnd))
		{
			GetWindowRect(hwnd, it != g_prevWindowRect.end() ? &it->second : &g_prevWindowRect[hwnd]);
			RedrawWindow(hwnd, nullptr, nullptr, RDW_ERASE | RDW_FRAME | RDW_INVALIDATE | RDW_ALLCHILDREN);
		}
		else if (it != g_prevWindowRect.end())
		{
			g_prevWindowRect.erase(it);
		}
	}

	void removeDropShadow(HWND hwnd)
	{
		const auto style = GetClassLongPtr(hwnd, GCL_STYLE);
		if (style & CS_DROPSHADOW)
		{
			SetClassLongPtr(hwnd, GCL_STYLE, style ^ CS_DROPSHADOW);
		}
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
