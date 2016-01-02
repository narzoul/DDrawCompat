#define CINTERFACE
#define WIN32_LEAN_AND_MEAN

#include <unordered_map>

#include <oleacc.h>
#include <Windows.h>
#include <detours.h>

#include "CompatDirectDraw.h"
#include "CompatDirectDrawSurface.h"
#include "CompatGdiDcCache.h"
#include "CompatGdiSurface.h"
#include "CompatPrimarySurface.h"
#include "DDrawLog.h"
#include "DDrawProcs.h"
#include "DDrawScopedThreadLock.h"
#include "RealPrimarySurface.h"

namespace
{
	using CompatGdiDcCache::CompatDc;

	struct CaretData
	{
		HWND hwnd;
		long left;
		long top;
		long width;
		long height;
		bool isDrawn;
	};

	struct ExcludeClipRectsData
	{
		HDC compatDc;
		POINT clientOrigin;
		HWND rootWnd;
	};

	bool g_suppressGdiHooks = false;

	class HookRecursionGuard
	{
	public:
		HookRecursionGuard()
		{
			g_suppressGdiHooks = true;
		}

		~HookRecursionGuard()
		{
			g_suppressGdiHooks = false;
		}
	};

	auto g_origGetDc = &GetDC;
	auto g_origGetDcEx = &GetDCEx;
	auto g_origGetWindowDc = &GetWindowDC;
	auto g_origReleaseDc = &ReleaseDC;
	auto g_origBeginPaint = &BeginPaint;
	auto g_origEndPaint = &EndPaint;

	auto g_origCreateCaret = &CreateCaret;
	auto g_origShowCaret = &ShowCaret;
	auto g_origHideCaret = &HideCaret;

	std::unordered_map<HDC, CompatDc> g_dcToCompatDc;

	CaretData g_caret = {};

	CRITICAL_SECTION g_gdiCriticalSection;

	class GdiScopedThreadLock
	{
	public:
		GdiScopedThreadLock()
		{
			EnterCriticalSection(&g_gdiCriticalSection);
		}

		~GdiScopedThreadLock()
		{
			LeaveCriticalSection(&g_gdiCriticalSection);
		}
	};

	POINT getClientOrigin(HWND hwnd);
	HDC getCompatDc(HWND hwnd, HDC origDc, const POINT& origin);
	HDC releaseCompatDc(HDC hdc);

	LRESULT CALLBACK callWndRetProc(int nCode, WPARAM wParam, LPARAM lParam)
	{
		if (HC_ACTION == nCode)
		{
			auto ret = reinterpret_cast<CWPRETSTRUCT*>(lParam);
			if (WM_WINDOWPOSCHANGED == ret->message)
			{
				InvalidateRect(nullptr, nullptr, TRUE);
			}
			else if (WM_ERASEBKGND == ret->message && ret->lResult)
			{
				HDC origDc = reinterpret_cast<HDC>(ret->wParam);
				GdiScopedThreadLock gdiLock;
				if (g_dcToCompatDc.find(origDc) == g_dcToCompatDc.end())
				{
					HWND hwnd = WindowFromDC(origDc);
					POINT origin = {};
					ClientToScreen(hwnd, &origin);

					HDC compatDc = getCompatDc(hwnd, origDc, origin);
					if (compatDc != origDc)
					{
						SendMessage(hwnd, WM_ERASEBKGND, reinterpret_cast<WPARAM>(compatDc), 0);
						releaseCompatDc(compatDc);
					}
				}
			}
		}

		return CallNextHookEx(nullptr, nCode, wParam, lParam);
	}

	BOOL CALLBACK excludeClipRectsForOverlappingWindows(HWND hwnd, LPARAM lParam)
	{
		auto excludeClipRectsData = reinterpret_cast<ExcludeClipRectsData*>(lParam);
		if (hwnd == excludeClipRectsData->rootWnd)
		{
			return FALSE;
		}

		RECT rect = {};
		GetWindowRect(hwnd, &rect);
		OffsetRect(&rect, -excludeClipRectsData->clientOrigin.x, -excludeClipRectsData->clientOrigin.y);
		ExcludeClipRect(excludeClipRectsData->compatDc, rect.left, rect.top, rect.right, rect.bottom);
		return TRUE;
	}

	POINT getClientOrigin(HWND hwnd)
	{
		POINT origin = {};
		if (hwnd)
		{
			ClientToScreen(hwnd, &origin);
		}
		return origin;
	}

	HDC getCompatDc(HWND hwnd, HDC origDc, const POINT& origin)
	{
		GdiScopedThreadLock gdiLock;
		if (!origDc || !RealPrimarySurface::isFullScreen() || g_suppressGdiHooks)
		{
			return origDc;
		}

		HookRecursionGuard recursionGuard;
		CompatDc compatDc = CompatGdiDcCache::getDc();
		if (!compatDc.dc)
		{
			return origDc;
		}

		if (hwnd)
		{
			SetWindowOrgEx(compatDc.dc, -origin.x, -origin.y, nullptr);

			HRGN clipRgn = CreateRectRgn(0, 0, 0, 0);
			GetRandomRgn(origDc, clipRgn, SYSRGN);
			SelectClipRgn(compatDc.dc, clipRgn);
			RECT r = {};
			GetRgnBox(clipRgn, &r);
			DeleteObject(clipRgn);

			ExcludeClipRectsData excludeClipRectsData = { compatDc.dc, origin, GetAncestor(hwnd, GA_ROOT) };
			EnumThreadWindows(GetCurrentThreadId(), &excludeClipRectsForOverlappingWindows,
				reinterpret_cast<LPARAM>(&excludeClipRectsData));
		}

		compatDc.origDc = origDc;
		g_dcToCompatDc[compatDc.dc] = compatDc;
		return compatDc.dc;
	}

	FARPROC getProcAddress(HMODULE module, const char* procName)
	{
		if (!module || !procName)
		{
			return nullptr;
		}

		PIMAGE_DOS_HEADER dosHeader = reinterpret_cast<PIMAGE_DOS_HEADER>(module);
		if (IMAGE_DOS_SIGNATURE != dosHeader->e_magic) {
			return nullptr;
		}
		char* moduleBase = reinterpret_cast<char*>(module);

		PIMAGE_NT_HEADERS ntHeader = reinterpret_cast<PIMAGE_NT_HEADERS>(
			reinterpret_cast<char*>(dosHeader) + dosHeader->e_lfanew);
		if (IMAGE_NT_SIGNATURE != ntHeader->Signature)
		{
			return nullptr;
		}

		PIMAGE_EXPORT_DIRECTORY exportDir = reinterpret_cast<PIMAGE_EXPORT_DIRECTORY>(
			moduleBase + ntHeader->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT].VirtualAddress);

		DWORD* rvaOfNames = reinterpret_cast<DWORD*>(moduleBase + exportDir->AddressOfNames);

		for (DWORD i = 0; i < exportDir->NumberOfNames; ++i)
		{
			if (0 == strcmp(procName, moduleBase + rvaOfNames[i]))
			{
				WORD* nameOrds = reinterpret_cast<WORD*>(moduleBase + exportDir->AddressOfNameOrdinals);
				DWORD* rvaOfFunctions = reinterpret_cast<DWORD*>(moduleBase + exportDir->AddressOfFunctions);
				return reinterpret_cast<FARPROC>(moduleBase + rvaOfFunctions[nameOrds[i]]);
			}
		}

		return nullptr;
	}

	POINT getWindowOrigin(HWND hwnd)
	{
		POINT origin = {};
		if (hwnd)
		{
			RECT windowRect = {};
			GetWindowRect(hwnd, &windowRect);
			origin.x = windowRect.left;
			origin.y = windowRect.top;
		}
		return origin;
	}

	template <typename FuncPtr>
	void hookGdiFunction(const char* funcName, FuncPtr& origFuncPtr, FuncPtr newFuncPtr)
	{
		origFuncPtr = reinterpret_cast<FuncPtr>(getProcAddress(GetModuleHandle("user32"), funcName));
		DetourAttach(reinterpret_cast<void**>(&origFuncPtr), newFuncPtr);
	}

	HDC releaseCompatDc(HDC hdc)
	{
		GdiScopedThreadLock gdiLock;
		if (g_suppressGdiHooks)
		{
			return hdc;
		}

		HookRecursionGuard recursionGuard;

		auto it = g_dcToCompatDc.find(hdc);

		if (it != g_dcToCompatDc.end())
		{
			HDC origDc = it->second.origDc;
			CompatGdiDcCache::returnDc(it->second);
			g_dcToCompatDc.erase(it);

			RealPrimarySurface::update();
			return origDc;
		}

		return hdc;
	}

	void drawCaret()
	{
		HDC dc = GetDC(g_caret.hwnd);
		PatBlt(dc, g_caret.left, g_caret.top, g_caret.width, g_caret.height, PATINVERT);
		ReleaseDC(g_caret.hwnd, dc);
	}

	LONG getCaretState(IAccessible* accessible)
	{
		VARIANT varChild = {};
		varChild.vt = VT_I4;
		varChild.lVal = CHILDID_SELF;
		VARIANT varState = {};
		accessible->lpVtbl->get_accState(accessible, varChild, &varState);
		return varState.lVal;
	}

	void CALLBACK caretDestroyEvent(
		HWINEVENTHOOK /*hWinEventHook*/,
		DWORD /*event*/,
		HWND hwnd,
		LONG idObject,
		LONG idChild,
		DWORD /*dwEventThread*/,
		DWORD /*dwmsEventTime*/)
	{
		if (OBJID_CARET != idObject || !g_caret.isDrawn || g_caret.hwnd != hwnd)
		{
			return;
		}

		IAccessible* accessible = nullptr;
		VARIANT varChild = {};
		AccessibleObjectFromEvent(hwnd, idObject, idChild, &accessible, &varChild);
		if (accessible)
		{
			if (STATE_SYSTEM_INVISIBLE == getCaretState(accessible))
			{
				GdiScopedThreadLock gdiLock;
				drawCaret();
				g_caret.isDrawn = false;
			}
			accessible->lpVtbl->Release(accessible);
		}
	}

	HDC WINAPI getDc(HWND hWnd)
	{
		Compat::LogEnter("GetDC", hWnd);
		HDC compatDc = getCompatDc(hWnd, g_origGetDc(hWnd), getClientOrigin(hWnd));
		Compat::LogLeave("GetDC", hWnd) << compatDc;
		return compatDc;
	}

	HDC WINAPI getDcEx(HWND hWnd, HRGN hrgnClip, DWORD flags)
	{
		Compat::LogEnter("GetDCEx", hWnd);
		HDC compatDc = getCompatDc(hWnd, g_origGetDcEx(hWnd, hrgnClip, flags),
			flags & (DCX_WINDOW | DCX_PARENTCLIP) ? getWindowOrigin(hWnd) : getClientOrigin(hWnd));
		Compat::LogLeave("GetDCEx", hWnd) << compatDc;
		return compatDc;
	}

	HDC WINAPI getWindowDc(HWND hWnd)
	{
		Compat::LogEnter("GetWindowDC", hWnd);
		HDC compatDc = getCompatDc(hWnd, g_origGetWindowDc(hWnd), getWindowOrigin(hWnd));
		Compat::LogLeave("GetWindowDC", hWnd) << compatDc;
		return compatDc;
	}

	int WINAPI releaseDc(HWND hWnd, HDC hDC)
	{
		Compat::LogEnter("ReleaseDC", hWnd, hDC);
		int result = g_origReleaseDc(hWnd, releaseCompatDc(hDC));
		Compat::LogLeave("ReleaseDC", hWnd, hDC) << result;
		return result;
	}

	HDC WINAPI beginPaint(HWND hWnd, LPPAINTSTRUCT lpPaint)
	{
		Compat::LogEnter("BeginPaint", hWnd, lpPaint);
		HDC compatDc = getCompatDc(hWnd, g_origBeginPaint(hWnd, lpPaint), getClientOrigin(hWnd));
		lpPaint->hdc = compatDc;
		Compat::LogLeave("BeginPaint", hWnd, lpPaint) << compatDc;
		return compatDc;
	}

	BOOL WINAPI endPaint(HWND hWnd, const PAINTSTRUCT* lpPaint)
	{
		Compat::LogEnter("EndPaint", hWnd, lpPaint);

		BOOL result = FALSE;
		if (lpPaint)
		{
			PAINTSTRUCT paint = *lpPaint;
			paint.hdc = releaseCompatDc(lpPaint->hdc);
			result = g_origEndPaint(hWnd, &paint);
		}
		else
		{
			result = g_origEndPaint(hWnd, lpPaint);
		}

		Compat::LogLeave("EndPaint", hWnd, lpPaint) << result;
		return result;
	}

	BOOL WINAPI createCaret(HWND hWnd, HBITMAP hBitmap, int nWidth, int nHeight)
	{
		BOOL result = g_origCreateCaret(hWnd, hBitmap, nWidth, nHeight);
		if (result)
		{
			GdiScopedThreadLock gdiLock;
			if (g_caret.isDrawn)
			{
				drawCaret();
				g_caret.isDrawn = false;
			}
			g_caret.width = nWidth ? nWidth : GetSystemMetrics(SM_CXBORDER);
			g_caret.height = nHeight ? nHeight : GetSystemMetrics(SM_CYBORDER);
		}
		return result;
	}

	BOOL WINAPI showCaret(HWND hWnd)
	{
		BOOL result = g_origShowCaret(hWnd);
		GdiScopedThreadLock gdiLock;
		if (result && !g_caret.isDrawn)
		{
			IAccessible* accessible = nullptr;
			AccessibleObjectFromWindow(hWnd, static_cast<DWORD>(OBJID_CARET), IID_IAccessible,
				reinterpret_cast<void**>(&accessible));
			if (accessible)
			{
				if (0 == getCaretState(accessible))
				{
					POINT caretPos = {};
					GetCaretPos(&caretPos);
					g_caret.left = caretPos.x;
					g_caret.top = caretPos.y;
					g_caret.hwnd = hWnd;
					drawCaret();
					g_caret.isDrawn = true;
				}
				accessible->lpVtbl->Release(accessible);
			}
		}
		return result;
	}

	BOOL WINAPI hideCaret(HWND hWnd)
	{
		BOOL result = g_origHideCaret(hWnd);
		GdiScopedThreadLock gdiLock;
		if (result && g_caret.isDrawn)
		{
			drawCaret();
			g_caret.isDrawn = false;
		}
		return result;
	}
}

void CompatGdiSurface::hookGdi()
{
	static bool alreadyHooked = false;
	if (alreadyHooked)
	{
		return;
	}

	InitializeCriticalSection(&g_gdiCriticalSection);
	if (CompatGdiDcCache::init())
	{
		DetourTransactionBegin();
		hookGdiFunction("GetDC", g_origGetDc, &getDc);
		hookGdiFunction("GetDCEx", g_origGetDcEx, &getDcEx);
		hookGdiFunction("GetWindowDC", g_origGetWindowDc, &getWindowDc);
		hookGdiFunction("ReleaseDC", g_origReleaseDc, &releaseDc);
		hookGdiFunction("BeginPaint", g_origBeginPaint, &beginPaint);
		hookGdiFunction("EndPaint", g_origEndPaint, &endPaint);
		hookGdiFunction("CreateCaret", g_origCreateCaret, &createCaret);
		hookGdiFunction("ShowCaret", g_origShowCaret, &showCaret);
		hookGdiFunction("HideCaret", g_origHideCaret, &hideCaret);
		DetourTransactionCommit();

		DWORD threadId = GetCurrentThreadId();
		SetWindowsHookEx(WH_CALLWNDPROCRET, callWndRetProc, nullptr, threadId);
		SetWinEventHook(EVENT_OBJECT_DESTROY, EVENT_OBJECT_DESTROY,
			nullptr, &caretDestroyEvent, 0, threadId, WINEVENT_OUTOFCONTEXT);
	}
	alreadyHooked = true;
}

void CompatGdiSurface::release()
{
	GdiScopedThreadLock gdiLock;
	CompatGdiDcCache::release();
}

void CompatGdiSurface::setSurfaceMemory(void* surfaceMemory, LONG pitch)
{
	GdiScopedThreadLock gdiLock;
	const bool wasReleased = CompatGdiDcCache::isReleased();
	CompatGdiDcCache::setSurfaceMemory(surfaceMemory, pitch);
	if (wasReleased)
	{
		InvalidateRect(nullptr, nullptr, TRUE);
	}
}
