#include <vector>

#include "Common/Hook.h"
#include "Common/Log.h"
#include "DDraw/RealPrimarySurface.h"
#include "Gdi/AccessGuard.h"
#include "Gdi/Dc.h"
#include "Gdi/Gdi.h"
#include "Gdi/PaintHandlers.h"
#include "Gdi/ScrollBar.h"
#include "Gdi/ScrollFunctions.h"
#include "Gdi/TitleBar.h"
#include "Gdi/VirtualScreen.h"
#include "Gdi/Window.h"
#include "Win32/Registry.h"

namespace
{
	typedef LRESULT(*WndProcHook)(HWND, UINT, WPARAM, LPARAM, WNDPROC);

	struct User32WndProc
	{
		WNDPROC oldWndProcTrampoline;
		WNDPROC oldWndProc;
		WNDPROC newWndProc;
		std::string name;
	
		User32WndProc()
			: oldWndProcTrampoline(nullptr)
			, oldWndProc(nullptr)
			, newWndProc(nullptr)
		{
		}
	};

	LRESULT defPaintProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, WNDPROC origWndProc);
	LRESULT defPaintProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, WNDPROC origWndProc,
		const char* origWndProcName);
	LRESULT onEraseBackground(HWND hwnd, HDC dc, WNDPROC origWndProc);
	LRESULT onNcPaint(HWND hwnd, WPARAM wParam, WNDPROC origWndProc);
	LRESULT onPaint(HWND hwnd, WNDPROC origWndProc);
	LRESULT onPrint(HWND hwnd, UINT msg, HDC dc, LONG flags, WNDPROC origWndProc);

	int g_menuWndProcIndex = 0;
	int g_scrollBarWndProcIndex = 0;

	std::vector<User32WndProc> g_user32WndProcA;
	std::vector<User32WndProc> g_user32WndProcW;
	std::vector<WndProcHook> g_user32WndProcHook;

	LRESULT buttonWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, WNDPROC origWndProc)
	{
		switch (msg)
		{
		case WM_PAINT:
			return onPaint(hwnd, origWndProc);

		case WM_ENABLE:
		case WM_SETTEXT:
		case BM_SETCHECK:
		case BM_SETSTATE:
		{
			LRESULT result = CallWindowProc(origWndProc, hwnd, msg, wParam, lParam);
			RedrawWindow(hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW);
			return result;
		}

		default:
			return CallWindowProc(origWndProc, hwnd, msg, wParam, lParam);
		}
	}

	LRESULT comboBoxWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, WNDPROC origWndProc)
	{
		return defPaintProc(hwnd, msg, wParam, lParam, origWndProc);
	}

	LRESULT comboListBoxWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, WNDPROC origWndProc)
	{
		LRESULT result = defPaintProc(hwnd, msg, wParam, lParam, origWndProc);

		switch (msg)
		{
		case WM_NCPAINT:
			CallWindowProc(origWndProc, hwnd, msg, wParam, lParam);
			break;
		}

		return result;
	}

	LRESULT WINAPI defDlgProcA(HWND hdlg, UINT msg, WPARAM wParam, LPARAM lParam)
	{
		return defPaintProc(hdlg, msg, wParam, lParam, CALL_ORIG_FUNC(DefDlgProcA), "defDlgProcA");
	}

	LRESULT WINAPI defDlgProcW(HWND hdlg, UINT msg, WPARAM wParam, LPARAM lParam)
	{
		return defPaintProc(hdlg, msg, wParam, lParam, CALL_ORIG_FUNC(DefDlgProcW), "defDlgProcW");
	}

	LRESULT defPaintProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, WNDPROC origWndProc)
	{
		switch (msg)
		{
		case WM_ERASEBKGND:
			return onEraseBackground(hwnd, reinterpret_cast<HDC>(wParam), origWndProc);

		case WM_NCPAINT:
			return onNcPaint(hwnd, wParam, origWndProc);

		case WM_PRINT:
		case WM_PRINTCLIENT:
			return onPrint(hwnd, msg, reinterpret_cast<HDC>(wParam), lParam, origWndProc);

		default:
			return CallWindowProc(origWndProc, hwnd, msg, wParam, lParam);
		}
	}

	LRESULT defPaintProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, WNDPROC origWndProc,
		const char* origWndProcName)
	{
		LOG_FUNC(origWndProcName, hwnd, Compat::hex(msg), Compat::hex(wParam), Compat::hex(lParam));
		return LOG_RESULT(defPaintProc(hwnd, msg, wParam, lParam, origWndProc));
	}

	LRESULT WINAPI defWindowProcA(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
	{
		return defPaintProc(hwnd, msg, wParam, lParam, CALL_ORIG_FUNC(DefWindowProcA), "defWindowProcA");
	}

	LRESULT WINAPI defWindowProcW(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
	{
		return defPaintProc(hwnd, msg, wParam, lParam, CALL_ORIG_FUNC(DefWindowProcW), "defWindowProcW");
	}

	void disableImmersiveContextMenus()
	{
		// Immersive context menus don't display properly (empty items) when theming is disabled
		Win32::Registry::setValue(
			HKEY_LOCAL_MACHINE,
			"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\FlightedFeatures",
			"ImmersiveContextMenu",
			0);

		// An update in Windows 10 seems to have moved the key from the above location
		Win32::Registry::setValue(
			HKEY_LOCAL_MACHINE,
			"Software\\Microsoft\\Windows\\CurrentVersion\\FlightedFeatures",
			"ImmersiveContextMenu",
			0);
	}

	LRESULT editWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, WNDPROC origWndProc)
	{
		LRESULT result = defPaintProc(hwnd, msg, wParam, lParam, origWndProc);
		if (0 == result && (WM_HSCROLL == msg || WM_VSCROLL == msg))
		{
			Gdi::ScrollFunctions::updateScrolledWindow(hwnd);
		}
		return result;
	}

	void hookUser32WndProc(const std::string& wndProcName, HWND hwnd, WNDPROC newWndProc,
		decltype(GetWindowLongPtr)* getWindowLong, std::vector<User32WndProc>& user32WndProc)
	{
		User32WndProc wndProc;
		wndProc.oldWndProc =
			reinterpret_cast<WNDPROC>(getWindowLong(hwnd, GWL_WNDPROC));
		wndProc.oldWndProcTrampoline = wndProc.oldWndProc;
		wndProc.newWndProc = newWndProc;
		wndProc.name = wndProcName;
		user32WndProc.push_back(wndProc);

		if (reinterpret_cast<DWORD>(wndProc.oldWndProcTrampoline) < 0xFFFF0000)
		{
			Compat::hookFunction(
				reinterpret_cast<void*&>(user32WndProc.back().oldWndProcTrampoline), newWndProc);
		}
	}

	void hookUser32WndProcA(const char* className, WNDPROC newWndProc, const std::string& wndProcName)
	{
		CLIENTCREATESTRUCT ccs = {};
		HWND hwnd = CreateWindowA(className, "", 0, 0, 0, 0, 0, 0, 0, 0, &ccs);
		hookUser32WndProc(wndProcName + 'A', hwnd, newWndProc, CALL_ORIG_FUNC(GetWindowLongA), g_user32WndProcA);
		DestroyWindow(hwnd);
	}

	void hookUser32WndProcW(const char* name, WNDPROC newWndProc, const std::string& wndProcName)
	{
		CLIENTCREATESTRUCT ccs = {};
		HWND hwnd = CreateWindowW(
			std::wstring(name, name + std::strlen(name)).c_str(), L"", 0, 0, 0, 0, 0, 0, 0, 0, &ccs);
		hookUser32WndProc(wndProcName + 'W', hwnd, newWndProc, CALL_ORIG_FUNC(GetWindowLongW), g_user32WndProcW);
		DestroyWindow(hwnd);
	}

	LRESULT listBoxWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, WNDPROC origWndProc)
	{
		return defPaintProc(hwnd, msg, wParam, lParam, origWndProc);
	}

	LRESULT mdiClientWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, WNDPROC origWndProc)
	{
		return defPaintProc(hwnd, msg, wParam, lParam, origWndProc);
	}

	LRESULT menuWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, WNDPROC origWndProc)
	{
		switch (msg)
		{
		case WM_NCPAINT:
			CallWindowProc(origWndProc, hwnd, msg, wParam, lParam);
			return onNcPaint(hwnd, wParam, origWndProc);

		case WM_PAINT:
			return onPaint(hwnd, origWndProc);

		case WM_PRINTCLIENT:
		{
			RECT r = {};
			GetClientRect(hwnd, &r);
			HDC dc = CreateCompatibleDC(nullptr);
			HBITMAP dib = Gdi::VirtualScreen::createOffScreenDib(r.right, r.bottom);
			HGDIOBJ origBitmap = SelectObject(dc, dib);
			CallWindowProc(origWndProc, hwnd, WM_ERASEBKGND, reinterpret_cast<WPARAM>(dc), 0);
			LRESULT result = CallWindowProc(origWndProc, hwnd, msg, reinterpret_cast<WPARAM>(dc), lParam);
			CALL_ORIG_FUNC(BitBlt)(reinterpret_cast<HDC>(wParam), 0, 0, r.right, r.bottom, dc, 0, 0, SRCCOPY);
			SelectObject(dc, origBitmap);
			DeleteObject(dib);
			DeleteDC(dc);
			return result;
		}

		case 0x1e5:
			if (-1 == wParam)
			{
				RedrawWindow(hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE);
			}
			return CallWindowProc(origWndProc, hwnd, msg, wParam, lParam);

		default:
			return defPaintProc(hwnd, msg, wParam, lParam, origWndProc);
		}
	}

	LRESULT onEraseBackground(HWND hwnd, HDC dc, WNDPROC origWndProc)
	{
		if (hwnd)
		{
			LRESULT result = 0;
			HDC compatDc = Gdi::Dc::getDc(dc);
			if (compatDc)
			{
				Gdi::GdiAccessGuard accessGuard(Gdi::ACCESS_WRITE);
				result = CallWindowProc(origWndProc, hwnd, WM_ERASEBKGND, reinterpret_cast<WPARAM>(compatDc), 0);
				Gdi::Dc::releaseDc(dc);
				return result;
			}
		}

		return CallWindowProc(origWndProc, hwnd, WM_ERASEBKGND, reinterpret_cast<WPARAM>(dc), 0);
	}

	LRESULT onNcPaint(HWND hwnd, WPARAM wParam, WNDPROC origWndProc)
	{
		if (!hwnd)
		{
			return CallWindowProc(origWndProc, hwnd, WM_NCPAINT, wParam, 0);
		}

		HDC windowDc = GetWindowDC(hwnd);
		HDC compatDc = Gdi::Dc::getDc(windowDc);

		if (compatDc)
		{
			Gdi::GdiAccessGuard accessGuard(Gdi::ACCESS_WRITE);
			Gdi::TitleBar titleBar(hwnd, compatDc);
			titleBar.drawAll();
			titleBar.excludeFromClipRegion();

			Gdi::ScrollBar scrollBar(hwnd, compatDc);
			scrollBar.drawAll();
			scrollBar.excludeFromClipRegion();

			CallWindowProc(origWndProc, hwnd, WM_PRINT, reinterpret_cast<WPARAM>(compatDc), PRF_NONCLIENT);

			Gdi::Dc::releaseDc(windowDc);
		}

		CALL_ORIG_FUNC(ReleaseDC)(hwnd, windowDc);
		return 0;
	}

	LRESULT onPaint(HWND hwnd, WNDPROC origWndProc)
	{
		if (!hwnd)
		{
			return CallWindowProc(origWndProc, hwnd, WM_PAINT, 0, 0);
		}

		DDraw::ScopedThreadLock lock;
		PAINTSTRUCT paint = {};
		HDC dc = BeginPaint(hwnd, &paint);
		HDC compatDc = Gdi::Dc::getDc(dc);

		if (compatDc)
		{
			Gdi::GdiAccessGuard accessGuard(Gdi::ACCESS_WRITE);
			CallWindowProc(origWndProc, hwnd, WM_PRINTCLIENT,
				reinterpret_cast<WPARAM>(compatDc), PRF_CLIENT);
			Gdi::Dc::releaseDc(dc);
		}
		else
		{
			CallWindowProc(origWndProc, hwnd, WM_PRINTCLIENT, reinterpret_cast<WPARAM>(dc), PRF_CLIENT);
		}

		EndPaint(hwnd, &paint);

		return 0;
	}

	LRESULT onPrint(HWND hwnd, UINT msg, HDC dc, LONG flags, WNDPROC origWndProc)
	{
		LRESULT result = 0;
		HDC compatDc = Gdi::Dc::getDc(dc);
		if (compatDc)
		{
			Gdi::GdiAccessGuard accessGuard(Gdi::ACCESS_WRITE);
			result = CallWindowProc(origWndProc, hwnd, msg, reinterpret_cast<WPARAM>(compatDc), flags);
			Gdi::Dc::releaseDc(dc);
		}
		else
		{
			result = CallWindowProc(origWndProc, hwnd, msg, reinterpret_cast<WPARAM>(dc), flags);
		}
		return result;
	}

	LRESULT scrollBarWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, WNDPROC origWndProc)
	{
		switch (msg)
		{
		case WM_PAINT:
			return onPaint(hwnd, origWndProc);

		case WM_SETCURSOR:
			if (GetWindowLong(hwnd, GWL_STYLE) & (SBS_SIZEBOX | SBS_SIZEGRIP))
			{
				SetCursor(LoadCursor(nullptr, IDC_SIZENWSE));
			}
			return TRUE;

		default:
			return CallWindowProc(origWndProc, hwnd, msg, wParam, lParam);
		}
	}

	LRESULT staticWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, WNDPROC origWndProc)
	{
		return defPaintProc(hwnd, msg, wParam, lParam, origWndProc);
	}

	LRESULT CALLBACK user32WndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam,
		const User32WndProc& user32WndProc, WndProcHook wndProcHook)
	{
		LOG_FUNC(user32WndProc.name.c_str(), hwnd, Compat::hex(uMsg), Compat::hex(wParam), Compat::hex(lParam));
		return LOG_RESULT(wndProcHook(hwnd, uMsg, wParam, lParam, user32WndProc.oldWndProcTrampoline));
	}

	template <int index>
	LRESULT CALLBACK user32WndProcA(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
	{
		return user32WndProc(hwnd, uMsg, wParam, lParam, g_user32WndProcA[index], g_user32WndProcHook[index]);
	}

	template <int index>
	LRESULT CALLBACK user32WndProcW(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
	{
		return user32WndProc(hwnd, uMsg, wParam, lParam, g_user32WndProcW[index], g_user32WndProcHook[index]);
	}
}

namespace Gdi
{
	namespace PaintHandlers
	{
		void installHooks()
		{
			disableImmersiveContextMenus();

#define HOOK_USER32_WNDPROC(index, name, wndProcHook) \
	g_user32WndProcHook.push_back(wndProcHook); \
	hookUser32WndProcA(name, user32WndProcA<index>, #wndProcHook); \
	hookUser32WndProcW(name, user32WndProcW<index>, #wndProcHook)

			g_user32WndProcA.reserve(9);
			g_user32WndProcW.reserve(9);

			HOOK_USER32_WNDPROC(0, "Button", buttonWndProc);
			HOOK_USER32_WNDPROC(1, "ComboBox", comboBoxWndProc);
			HOOK_USER32_WNDPROC(2, "Edit", editWndProc);
			HOOK_USER32_WNDPROC(3, "ListBox", listBoxWndProc);
			HOOK_USER32_WNDPROC(4, "MDIClient", mdiClientWndProc);
			HOOK_USER32_WNDPROC(5, "ScrollBar", scrollBarWndProc);
			HOOK_USER32_WNDPROC(6, "Static", staticWndProc);
			HOOK_USER32_WNDPROC(7, "ComboLBox", comboListBoxWndProc);
			HOOK_USER32_WNDPROC(8, "#32768", menuWndProc);

			g_scrollBarWndProcIndex = 5;
			g_menuWndProcIndex = 8;

#undef HOOK_USER32_WNDPROC

			HOOK_FUNCTION(user32, DefWindowProcA, defWindowProcA);
			HOOK_FUNCTION(user32, DefWindowProcW, defWindowProcW);
			HOOK_FUNCTION(user32, DefDlgProcA, defDlgProcA);
			HOOK_FUNCTION(user32, DefDlgProcW, defDlgProcW);
		}

		void onCreateWindow(HWND hwnd)
		{
			WNDPROC wndProcA = reinterpret_cast<WNDPROC>(GetWindowLongA(hwnd, GWL_WNDPROC));
			WNDPROC wndProcW = reinterpret_cast<WNDPROC>(GetWindowLongW(hwnd, GWL_WNDPROC));

			int index = -1;
			if (wndProcA == g_user32WndProcA[g_menuWndProcIndex].oldWndProc ||
				wndProcW == g_user32WndProcW[g_menuWndProcIndex].oldWndProc)
			{
				index = g_menuWndProcIndex;
			}
			else if (wndProcA == g_user32WndProcA[g_scrollBarWndProcIndex].oldWndProc ||
				wndProcW == g_user32WndProcW[g_scrollBarWndProcIndex].oldWndProc)
			{
				index = g_scrollBarWndProcIndex;
			}

			if (-1 != index)
			{
				if (IsWindowUnicode(hwnd))
				{
					CALL_ORIG_FUNC(SetWindowLongW)(hwnd, GWL_WNDPROC,
						reinterpret_cast<LONG>(g_user32WndProcW[index].newWndProc));
				}
				else
				{
					CALL_ORIG_FUNC(SetWindowLongA)(hwnd, GWL_WNDPROC,
						reinterpret_cast<LONG>(g_user32WndProcA[index].newWndProc));
				}
			}
		}
	}
}
