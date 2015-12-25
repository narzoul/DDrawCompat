#define CINTERFACE
#define WIN32_LEAN_AND_MEAN

#include <unordered_map>

#include <oleacc.h>
#include <Windows.h>
#include <detours.h>

#include "CompatDirectDraw.h"
#include "CompatDirectDrawSurface.h"
#include "CompatGdiSurface.h"
#include "CompatPrimarySurface.h"
#include "DDrawLog.h"
#include "DDrawProcs.h"
#include "RealPrimarySurface.h"

namespace
{
	struct CaretData
	{
		HWND hwnd;
		long left;
		long top;
		long width;
		long height;
		bool isDrawn;
	};

	struct GdiSurface
	{
		IDirectDrawSurface7* surface;
		HDC origDc;
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

	IDirectDraw7* g_directDraw = nullptr;
	std::unordered_map<HDC, GdiSurface> g_dcToSurface;

	CaretData g_caret = {};

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
				if (g_dcToSurface.find(origDc) == g_dcToSurface.end())
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

	IDirectDraw7* createDirectDraw()
	{
		IDirectDraw7* dd = nullptr;
		CALL_ORIG_DDRAW(DirectDrawCreateEx, nullptr, reinterpret_cast<LPVOID*>(&dd), IID_IDirectDraw7, nullptr);
		if (!dd)
		{
			Compat::Log() << "Failed to create a DirectDraw interface for GDI";
			return nullptr;
		}

		if (FAILED(CompatDirectDraw<IDirectDraw7>::s_origVtable.SetCooperativeLevel(dd, nullptr, DDSCL_NORMAL)))
		{
			Compat::Log() << "Failed to set the cooperative level on the DirectDraw interface for GDI";
			dd->lpVtbl->Release(dd);
			return nullptr;
		}

		return dd;
	}

	IDirectDrawSurface7* createGdiSurface()
	{
		DDSURFACEDESC2 desc = {};
		desc.dwSize = sizeof(desc);
		desc.dwFlags = DDSD_WIDTH | DDSD_HEIGHT | DDSD_PIXELFORMAT | DDSD_CAPS | DDSD_PITCH | DDSD_LPSURFACE;
		desc.dwWidth = CompatPrimarySurface::width;
		desc.dwHeight = CompatPrimarySurface::height;
		desc.ddpfPixelFormat = CompatPrimarySurface::pixelFormat;
		desc.ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN | DDSCAPS_SYSTEMMEMORY;
		desc.lPitch = CompatPrimarySurface::pitch;
		desc.lpSurface = CompatPrimarySurface::surfacePtr;

		IDirectDrawSurface7* surface = nullptr;
		HRESULT result = CompatDirectDraw<IDirectDraw7>::s_origVtable.CreateSurface(
			g_directDraw, &desc, &surface, nullptr);
		if (FAILED(result))
		{
			LOG_ONCE("Failed to create a GDI surface: " << result);
			return nullptr;
		}

		if (CompatPrimarySurface::palette)
		{
			CompatDirectDrawSurface<IDirectDrawSurface7>::s_origVtable.SetPalette(
				surface, CompatPrimarySurface::palette);
		}

		return surface;
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
		if (!CompatPrimarySurface::surfacePtr || !origDc || !RealPrimarySurface::isFullScreen() || g_suppressGdiHooks) 
		{
			return origDc;
		}

		HookRecursionGuard recursionGuard;

		IDirectDrawSurface7* surface = createGdiSurface();
		if (!surface)
		{
			return origDc;
		}

		HDC compatDc = nullptr;
		HRESULT result = surface->lpVtbl->GetDC(surface, &compatDc);
		if (FAILED(result))
		{
			LOG_ONCE("Failed to create a GDI DC: " << result);
			surface->lpVtbl->Release(surface);
			return origDc;
		}

		if (hwnd)
		{
			SetWindowOrgEx(compatDc, -origin.x, -origin.y, nullptr);

			HRGN clipRgn = CreateRectRgn(0, 0, 0, 0);
			GetRandomRgn(origDc, clipRgn, SYSRGN);
			SelectClipRgn(compatDc, clipRgn);
			RECT r = {};
			GetRgnBox(clipRgn, &r);
			DeleteObject(clipRgn);

			ExcludeClipRectsData excludeClipRectsData = { compatDc, origin, GetAncestor(hwnd, GA_ROOT) };
			EnumThreadWindows(GetCurrentThreadId(), &excludeClipRectsForOverlappingWindows,
				reinterpret_cast<LPARAM>(&excludeClipRectsData));
		}

		GdiSurface gdiSurface = { surface, origDc };
		g_dcToSurface[compatDc] = gdiSurface;

		// Release DD critical section acquired by IDirectDrawSurface7::GetDC to avoid deadlocks
		Compat::origProcs.ReleaseDDThreadLock();

		return compatDc;
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
		if (g_suppressGdiHooks)
		{
			return hdc;
		}

		HookRecursionGuard recursionGuard;

		auto it = g_dcToSurface.find(hdc);

		if (it != g_dcToSurface.end())
		{
			// Reacquire DD critical section that was temporarily released after IDirectDrawSurface7::GetDC
			Compat::origProcs.AcquireDDThreadLock();

			if (FAILED(it->second.surface->lpVtbl->ReleaseDC(it->second.surface, hdc)))
			{
				Compat::origProcs.ReleaseDDThreadLock();
			}

			it->second.surface->lpVtbl->Release(it->second.surface);

			HDC origDc = it->second.origDc;
			g_dcToSurface.erase(it);

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
				drawCaret();
				g_caret.isDrawn = false;
			}
			accessible->lpVtbl->Release(accessible);
		}
	}

	HDC WINAPI getDc(HWND hWnd)
	{
		Compat::origProcs.AcquireDDThreadLock();
		Compat::LogEnter("GetDC", hWnd);

		HDC compatDc = getCompatDc(hWnd, g_origGetDc(hWnd), getClientOrigin(hWnd));

		Compat::LogLeave("GetDC", hWnd) << compatDc;
		Compat::origProcs.ReleaseDDThreadLock();
		return compatDc;
	}

	HDC WINAPI getDcEx(HWND hWnd, HRGN hrgnClip, DWORD flags)
	{
		Compat::origProcs.AcquireDDThreadLock();
		Compat::LogEnter("GetDCEx", hWnd);

		HDC compatDc = getCompatDc(hWnd, g_origGetDcEx(hWnd, hrgnClip, flags),
			flags & (DCX_WINDOW | DCX_PARENTCLIP) ? getWindowOrigin(hWnd) : getClientOrigin(hWnd));

		Compat::LogLeave("GetDCEx", hWnd) << compatDc;
		Compat::origProcs.ReleaseDDThreadLock();
		return compatDc;
	}

	HDC WINAPI getWindowDc(HWND hWnd)
	{
		Compat::origProcs.AcquireDDThreadLock();
		Compat::LogEnter("GetWindowDC", hWnd);

		HDC compatDc = getCompatDc(hWnd, g_origGetWindowDc(hWnd), getWindowOrigin(hWnd));

		Compat::LogLeave("GetWindowDC", hWnd) << compatDc;
		Compat::origProcs.ReleaseDDThreadLock();
		return compatDc;
	}

	int WINAPI releaseDc(HWND hWnd, HDC hDC)
	{
		Compat::origProcs.AcquireDDThreadLock();
		Compat::LogEnter("ReleaseDC", hWnd, hDC);

		int result = g_origReleaseDc(hWnd, releaseCompatDc(hDC));

		Compat::LogLeave("ReleaseDC", hWnd, hDC) << result;
		Compat::origProcs.ReleaseDDThreadLock();
		return result;
	}

	HDC WINAPI beginPaint(HWND hWnd, LPPAINTSTRUCT lpPaint)
	{
		Compat::origProcs.AcquireDDThreadLock();
		Compat::LogEnter("BeginPaint", hWnd, lpPaint);

		HDC compatDc = getCompatDc(hWnd, g_origBeginPaint(hWnd, lpPaint), getClientOrigin(hWnd));
		lpPaint->hdc = compatDc;
		
		Compat::LogLeave("BeginPaint", hWnd, lpPaint) << compatDc;
		Compat::origProcs.ReleaseDDThreadLock();
		return compatDc;
	}

	BOOL WINAPI endPaint(HWND hWnd, const PAINTSTRUCT* lpPaint)
	{
		Compat::origProcs.AcquireDDThreadLock();
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
		Compat::origProcs.ReleaseDDThreadLock();
		return result;
	}

	BOOL WINAPI createCaret(HWND hWnd, HBITMAP hBitmap, int nWidth, int nHeight)
	{
		BOOL result = g_origCreateCaret(hWnd, hBitmap, nWidth, nHeight);
		if (result)
		{
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

	g_directDraw = createDirectDraw();
	if (g_directDraw)
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
