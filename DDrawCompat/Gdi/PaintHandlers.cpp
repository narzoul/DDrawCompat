#include <vector>

#include "Common/Hook.h"
#include "Common/Log.h"
#include "D3dDdi/ScopedCriticalSection.h"
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

std::ostream& operator<<(std::ostream& os, const MENUITEMINFOW& val)
{
	return Compat::LogStruct(os)
		<< val.cbSize
		<< Compat::hex(val.fMask)
		<< Compat::hex(val.fType)
		<< Compat::hex(val.fState)
		<< val.wID
		<< val.hSubMenu
		<< val.hbmpChecked
		<< val.hbmpUnchecked
		<< Compat::hex(val.dwItemData)
		<< val.dwTypeData
		<< val.cch
		<< (val.cbSize > offsetof(MENUITEMINFOW, hbmpItem) ? val.hbmpItem : nullptr);
}

namespace
{
	typedef LRESULT(*WndProcHook)(HWND, UINT, WPARAM, LPARAM, WNDPROC);

	struct User32WndProc
	{
		WNDPROC oldWndProc;
		WNDPROC oldWndProcTrampoline;
		WNDPROC newWndProc;
		std::string procName;
		std::string className;
		bool isUnicode;
	};

	LRESULT defPaintProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, WNDPROC origWndProc);
	LRESULT defPaintProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, WNDPROC origWndProc,
		const char* origWndProcName);
	LRESULT onEraseBackground(HWND hwnd, HDC dc, WNDPROC origWndProc);
	LRESULT onNcPaint(HWND hwnd, WPARAM wParam, WNDPROC origWndProc);
	LRESULT onPaint(HWND hwnd, WNDPROC origWndProc);
	LRESULT onPrint(HWND hwnd, UINT msg, HDC dc, LONG flags, WNDPROC origWndProc);

	User32WndProc* g_currentUser32WndProc = nullptr;
	std::vector<std::string> g_failedHooks;

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

	LRESULT CALLBACK cbtProc(int nCode, WPARAM wParam, LPARAM lParam)
	{
		if (HCBT_CREATEWND == nCode && g_currentUser32WndProc)
		{
			const auto hwnd = reinterpret_cast<HWND>(wParam);
			const bool isUnicode = IsWindowUnicode(hwnd);
			char className[32] = {};
			GetClassName(hwnd, className, sizeof(className));

			if (0 == _stricmp(className, g_currentUser32WndProc->className.c_str()) &&
				isUnicode == g_currentUser32WndProc->isUnicode &&
				!g_currentUser32WndProc->oldWndProc)
			{
				decltype(&GetWindowLong) getWindowLong = isUnicode ? GetWindowLongW : GetWindowLongA;
				auto wndProc = reinterpret_cast<WNDPROC>(getWindowLong(hwnd, GWL_WNDPROC));
				HMODULE wndProcModule = nullptr;
				GetModuleHandleEx(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
					reinterpret_cast<const char*>(wndProc), &wndProcModule);

				if (wndProcModule && wndProcModule == GetModuleHandle("user32"))
				{
					g_currentUser32WndProc->oldWndProc = wndProc;
					g_currentUser32WndProc->oldWndProcTrampoline = wndProc;
					Compat::hookFunction(reinterpret_cast<void*&>(g_currentUser32WndProc->oldWndProcTrampoline),
						g_currentUser32WndProc->newWndProc,
						g_currentUser32WndProc->procName.c_str());
				}
			}
		}

		return CallNextHookEx(nullptr, nCode, wParam, lParam);
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
		[[maybe_unused]] const char* origWndProcName)
	{
		LOG_FUNC(origWndProcName, hwnd, Compat::logWm(msg), Compat::hex(wParam), Compat::hex(lParam));
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

	LRESULT editWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, WNDPROC origWndProc)
	{
		LRESULT result = defPaintProc(hwnd, msg, wParam, lParam, origWndProc);
		if (0 == result && (WM_HSCROLL == msg || WM_VSCROLL == msg))
		{
			Gdi::ScrollFunctions::updateScrolledWindow(hwnd);
		}
		return result;
	}

	template <WndProcHook>
	User32WndProc& getUser32WndProcA()
	{
		static User32WndProc user32WndProcA = {};
		return user32WndProcA;
	}

	template <WndProcHook>
	User32WndProc& getUser32WndProcW()
	{
		static User32WndProc user32WndProcW = {};
		return user32WndProcW;
	}

	void hookUser32WndProc(User32WndProc& user32WndProc, WNDPROC newWndProc,
		const std::string& procName, const std::string& className, bool isUnicode)
	{
		CLIENTCREATESTRUCT ccs = {};
		user32WndProc.newWndProc = newWndProc;
		user32WndProc.procName = procName;
		user32WndProc.className = className;
		user32WndProc.isUnicode = isUnicode;

		g_currentUser32WndProc = &user32WndProc;
		if (isUnicode)
		{
			DestroyWindow(CreateWindowW(std::wstring(className.begin(), className.end()).c_str(), L"", 0, 0, 0, 0, 0,
				nullptr, nullptr, nullptr, &ccs));
		}
		else
		{
			DestroyWindow(CreateWindowA(className.c_str(), "", 0, 0, 0, 0, 0, nullptr, nullptr, nullptr, &ccs));
		}
		g_currentUser32WndProc = nullptr;

		if (user32WndProc.oldWndProc == user32WndProc.oldWndProcTrampoline)
		{
			g_failedHooks.push_back(user32WndProc.procName);
		}
	}

	template <WndProcHook wndProcHook>
	void hookUser32WndProcA(const std::string& className, const std::string& procName)
	{
		hookUser32WndProc(getUser32WndProcA<wndProcHook>(), user32WndProcA<wndProcHook>,
			procName + 'A', className, false);
	}

	template <WndProcHook wndProcHook>
	void hookUser32WndProcW(const std::string& className, const std::string& procName)
	{
		hookUser32WndProc(getUser32WndProcW<wndProcHook>(), user32WndProcW<wndProcHook>,
			procName + 'W', className, true);
	}

	template <WndProcHook wndProcHook>
	void hookUser32WndProc(const std::string& className, const std::string& procName)
	{
		hookUser32WndProcA<wndProcHook>(className, procName);
		hookUser32WndProcW<wndProcHook>(className, procName);
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
		{
			D3dDdi::ScopedCriticalSection lock;
			RedrawWindow(hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_FRAME);
			return onPaint(hwnd, origWndProc);
		}

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
				D3dDdi::ScopedCriticalSection lock;
				RedrawWindow(hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_UPDATENOW);
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
				Gdi::AccessGuard accessGuard(Gdi::ACCESS_WRITE);
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
			Gdi::AccessGuard accessGuard(Gdi::ACCESS_WRITE);
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

		PAINTSTRUCT paint = {};
		HDC dc = BeginPaint(hwnd, &paint);
		HDC compatDc = Gdi::Dc::getDc(dc);

		if (compatDc)
		{
			Gdi::AccessGuard accessGuard(Gdi::ACCESS_WRITE);
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
			Gdi::AccessGuard accessGuard(Gdi::ACCESS_WRITE);
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

	BOOL WINAPI setMenuItemInfoW(HMENU hmenu, UINT item, BOOL fByPositon, LPCMENUITEMINFOW lpmii)
	{
		LOG_FUNC("SetMenuItemInfoW", hmenu, item, fByPositon, lpmii);
		if (lpmii && (lpmii->fMask & (MIIM_TYPE | MIIM_FTYPE)) && MFT_OWNERDRAW == lpmii->fType)
		{
			SetLastError(ERROR_NOT_SUPPORTED);
			return LOG_RESULT(FALSE);
		}
		return LOG_RESULT(CALL_ORIG_FUNC(SetMenuItemInfoW)(hmenu, item, fByPositon, lpmii));
	}

	LRESULT staticWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, WNDPROC origWndProc)
	{
		return defPaintProc(hwnd, msg, wParam, lParam, origWndProc);
	}

	LRESULT CALLBACK user32WndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam,
		[[maybe_unused]] const std::string& procName, WndProcHook wndProcHook, WNDPROC oldWndProcTrampoline)
	{
		LOG_FUNC(procName.c_str(), hwnd, Compat::logWm(uMsg), Compat::hex(wParam), Compat::hex(lParam));
		return LOG_RESULT(wndProcHook(hwnd, uMsg, wParam, lParam, oldWndProcTrampoline));
	}

	template <WndProcHook wndProcHook>
	LRESULT CALLBACK user32WndProcA(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
	{
		auto& wp = getUser32WndProcA<wndProcHook>();
		return user32WndProc(hwnd, uMsg, wParam, lParam, wp.procName, wndProcHook, wp.oldWndProcTrampoline);
	}

	template <WndProcHook wndProcHook>
	LRESULT CALLBACK user32WndProcW(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
	{
		auto& wp = getUser32WndProcW<wndProcHook>();
		return user32WndProc(hwnd, uMsg, wParam, lParam, wp.procName, wndProcHook, wp.oldWndProcTrampoline);
	}
}

namespace Gdi
{
	namespace PaintHandlers
	{
		void installHooks()
		{
#define HOOK_USER32_WNDPROC(className, wndProcHook) hookUser32WndProc<wndProcHook>(className, #wndProcHook)
#define HOOK_USER32_WNDPROCW(className, wndProcHook) hookUser32WndProcW<wndProcHook>(className, #wndProcHook)

			auto hook = SetWindowsHookEx(WH_CBT, cbtProc, nullptr, GetCurrentThreadId());

			HOOK_USER32_WNDPROC("Button", buttonWndProc);
			HOOK_USER32_WNDPROC("ComboBox", comboBoxWndProc);
			HOOK_USER32_WNDPROC("Edit", editWndProc);
			HOOK_USER32_WNDPROC("ListBox", listBoxWndProc);
			HOOK_USER32_WNDPROC("MDIClient", mdiClientWndProc);
			HOOK_USER32_WNDPROC("Static", staticWndProc);
			HOOK_USER32_WNDPROC("ComboLBox", comboListBoxWndProc);

			HOOK_USER32_WNDPROCW("ScrollBar", scrollBarWndProc);
			HOOK_USER32_WNDPROCW("#32768", menuWndProc);

			UnhookWindowsHookEx(hook);

#undef HOOK_USER32_WNDPROC
#undef HOOK_USER32_WNDPROCW

			if (!g_failedHooks.empty())
			{
				Compat::Log() << "Warning: Failed to hook the following user32 window procedures: " <<
					Compat::array(g_failedHooks.data(), g_failedHooks.size());
				g_failedHooks.clear();
			}

			HOOK_FUNCTION(user32, DefWindowProcA, defWindowProcA);
			HOOK_FUNCTION(user32, DefWindowProcW, defWindowProcW);
			HOOK_FUNCTION(user32, DefDlgProcA, defDlgProcA);
			HOOK_FUNCTION(user32, DefDlgProcW, defDlgProcW);
			HOOK_FUNCTION(user32, SetMenuItemInfoW, setMenuItemInfoW);
		}

		void onCreateWindow(HWND hwnd)
		{
			if (!IsWindowUnicode(hwnd))
			{
				return;
			}

			const auto wndProc = reinterpret_cast<WNDPROC>(GetWindowLongW(hwnd, GWL_WNDPROC));
			User32WndProc* user32WndProc = nullptr;
			if (getUser32WndProcW<menuWndProc>().oldWndProc == wndProc)
			{
				user32WndProc = &getUser32WndProcW<menuWndProc>();
			}
			else if (getUser32WndProcW<scrollBarWndProc>().oldWndProc == wndProc)
			{
				user32WndProc = &getUser32WndProcW<scrollBarWndProc>();
			}

			if (user32WndProc)
			{
				SetWindowLongW(hwnd, GWL_WNDPROC, reinterpret_cast<LONG>(user32WndProc->newWndProc));
			}
		}
	}
}
