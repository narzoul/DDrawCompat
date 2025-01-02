#include <D3dDdi/ScopedCriticalSection.h>
#include <DDraw/RealPrimarySurface.h>
#include <DDraw/Surfaces/PrimarySurface.h>
#include <Gdi/CompatDc.h>
#include <Gdi/Cursor.h>
#include <Gdi/GuiThread.h>
#include <Gdi/ScrollBar.h>
#include <Gdi/ScrollFunctions.h>
#include <Gdi/TitleBar.h>
#include <Gdi/User32WndProcs.h>
#include <Gdi/VirtualScreen.h>
#include <Gdi/Window.h>
#include <Gdi/WinProc.h>

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

	template <WndProcHook>
	User32WndProc g_user32WndProcA = {};
	template <WndProcHook>
	User32WndProc g_user32WndProcW = {};

	LRESULT defPaintProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, WNDPROC origWndProc);
	LRESULT defPaintProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, WNDPROC origWndProc,
		const char* origWndProcName);
	LRESULT onEraseBackground(HWND hwnd, HDC dc, WNDPROC origWndProc);
	LRESULT onNcActivate(HWND hwnd, WPARAM wParam, LPARAM lParam);
	LRESULT onNcPaint(HWND hwnd, WNDPROC origWndProc);
	LRESULT onPaint(HWND hwnd, WNDPROC origWndProc);
	LRESULT onPrint(HWND hwnd, UINT msg, HDC dc, LONG flags, WNDPROC origWndProc);
	LRESULT onSetText(HWND hwnd, WPARAM wParam, LPARAM lParam, WNDPROC origWndProc);

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
			InvalidateRect(hwnd, nullptr, TRUE);
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
			return onNcPaint(hwnd, origWndProc);

		case WM_PRINT:
		case WM_PRINTCLIENT:
			return onPrint(hwnd, msg, reinterpret_cast<HDC>(wParam), lParam, origWndProc);

		case WM_SETTEXT:
			return onSetText(hwnd, wParam, lParam, origWndProc);

		default:
			return CallWindowProc(origWndProc, hwnd, msg, wParam, lParam);
		}
	}

	LRESULT defPaintProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, WNDPROC origWndProc,
		[[maybe_unused]] const char* origWndProcName)
	{
		LOG_FUNC(origWndProcName, Compat::WindowMessageStruct(hwnd, msg, wParam, lParam));
		return LOG_RESULT(defPaintProc(hwnd, msg, wParam, lParam, origWndProc));
	}

	LRESULT defWindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, WNDPROC origDefWindowProc)
	{
		switch (msg)
		{
		case WM_CREATE:
		{
			LRESULT result = origDefWindowProc(hwnd, msg, wParam, lParam);
			if (-1 != result)
			{
				Gdi::WinProc::onCreateWindow(hwnd);
			}
			return result;
		}

		case WM_CTLCOLORSCROLLBAR:
			if (reinterpret_cast<HWND>(lParam) == hwnd)
			{
				LRESULT result = origDefWindowProc(hwnd, msg, wParam, lParam);
				Gdi::ScrollBar::onCtlColorScrollBar(hwnd, wParam, lParam, result);
				return result;
			}
			break;

		case WM_ERASEBKGND:
		{
			HBRUSH brush = reinterpret_cast<HBRUSH>(GetClassLong(hwnd, GCL_HBRBACKGROUND));
			if (!brush)
			{
				return FALSE;
			}
			RECT rect = {};
			GetClientRect(hwnd, &rect);
			FillRect(Gdi::CompatDc(reinterpret_cast<HDC>(wParam)), &rect, brush);
			return TRUE;
		}

		case WM_NCACTIVATE:
			return onNcActivate(hwnd, wParam, lParam);

		case WM_NCCREATE:
		{
			char className[64] = {};
			GetClassName(hwnd, className, sizeof(className));
			if (std::string(className) == "CompatWindowDesktopReplacement")
			{
				// Disable VirtualizeDesktopPainting shim
				return FALSE;
			}

			LRESULT result = origDefWindowProc(hwnd, msg, wParam, lParam);
			if (result)
			{
				Gdi::WinProc::onCreateWindow(hwnd);
			}
			return result;
		}

		case WM_NCLBUTTONDOWN:
		{
			if (wParam == HTHSCROLL || wParam == HTVSCROLL)
			{
				Gdi::ScrollBar sb(hwnd, wParam == HTHSCROLL ? SB_HORZ : SB_VERT);
				sb.onLButtonDown(lParam);
				return origDefWindowProc(hwnd, msg, wParam, lParam);
			}
			return origDefWindowProc(hwnd, msg, wParam, lParam);
		}

		case WM_SETCURSOR:
		{
			switch (LOWORD(lParam))
			{
			case HTLEFT:
			case HTRIGHT:
				SetCursor(CALL_ORIG_FUNC(LoadCursorA)(nullptr, IDC_SIZEWE));
				return TRUE;

			case HTTOP:
			case HTBOTTOM:
				SetCursor(CALL_ORIG_FUNC(LoadCursorA)(nullptr, IDC_SIZENS));
				return TRUE;

			case HTTOPLEFT:
			case HTBOTTOMRIGHT:
				SetCursor(CALL_ORIG_FUNC(LoadCursorA)(nullptr, IDC_SIZENWSE));
				return TRUE;

			case HTBOTTOMLEFT:
			case HTTOPRIGHT:
				SetCursor(CALL_ORIG_FUNC(LoadCursorA)(nullptr, IDC_SIZENESW));
				return TRUE;
			}

			HWND parent = GetAncestor(hwnd, GA_PARENT);
			if (parent && GetDesktopWindow() != parent && SendMessage(parent, msg, wParam, lParam))
			{
				return TRUE;
			}

			if (hwnd != reinterpret_cast<HWND>(wParam))
			{
				return FALSE;
			}

			if (HTCLIENT == LOWORD(lParam))
			{
				auto cursor = GetClassLong(hwnd, GCL_HCURSOR);
				if (cursor)
				{
					SetCursor(reinterpret_cast<HCURSOR>(cursor));
					return TRUE;
				}
			}
			else
			{
				SetCursor(CALL_ORIG_FUNC(LoadCursorA)(nullptr, IDC_ARROW));
			}
			return FALSE;
		}

		case WM_SETREDRAW:
		{
			if (Gdi::Window::isTopLevelWindow(hwnd))
			{
				BOOL isVisible = IsWindowVisible(hwnd);
				auto result = origDefWindowProc(hwnd, msg, wParam, lParam);
				if (isVisible != IsWindowVisible(hwnd))
				{
					Gdi::Window::updateAll();
				}
				return result;
			}
			break;
		}
		}

		return defPaintProc(hwnd, msg, wParam, lParam, origDefWindowProc);
	}

	LRESULT WINAPI defWindowProcA(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
	{
		LOG_FUNC("DefWindowProcA", Compat::WindowMessageStruct(hwnd, msg, wParam, lParam));
		return LOG_RESULT(defWindowProc(hwnd, msg, wParam, lParam, CALL_ORIG_FUNC(DefWindowProcA)));
	}

	LRESULT WINAPI defWindowProcW(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
	{
		LOG_FUNC("DefWindowProcW", Compat::WindowMessageStruct(hwnd, msg, wParam, lParam));
		return LOG_RESULT(defWindowProc(hwnd, msg, wParam, lParam, CALL_ORIG_FUNC(DefWindowProcW)));
	}

	LRESULT editWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, WNDPROC origWndProc)
	{
		switch (msg)
		{
		case WM_MOUSEMOVE:
			if (!(wParam & MK_LBUTTON))
			{
				break;
			}
		case 0x0118:
		case WM_HSCROLL:
		case WM_KEYDOWN:
		case WM_MOUSEHWHEEL:
		case WM_MOUSEWHEEL:
		case WM_VSCROLL:
		{
			int horz = GetScrollPos(hwnd, SB_HORZ);
			int vert = GetScrollPos(hwnd, SB_VERT);
			LRESULT firstVisibleLine = CallWindowProc(origWndProc, hwnd, EM_GETFIRSTVISIBLELINE, 0, 0);
			LRESULT result = CallWindowProc(origWndProc, hwnd, msg, wParam, lParam);
			if (firstVisibleLine != CallWindowProc(origWndProc, hwnd, EM_GETFIRSTVISIBLELINE, 0, 0) ||
				horz != GetScrollPos(hwnd, SB_HORZ) ||
				vert != GetScrollPos(hwnd, SB_VERT))
			{
				RedrawWindow(hwnd, nullptr, nullptr, RDW_ERASE | RDW_INVALIDATE | RDW_UPDATENOW);
			}
			return result;
		}
		}

		return defPaintProc(hwnd, msg, wParam, lParam, origWndProc);
	}

	void fixPopupMenuPosition(WINDOWPOS& wp)
	{
		RECT mr = Win32::DisplayMode::getMonitorInfo(MonitorFromWindow(wp.hwnd, MONITOR_DEFAULTTOPRIMARY)).rcEmulated;
		if (wp.flags & SWP_NOSIZE)
		{
			RECT r = {};
			CALL_ORIG_FUNC(GetWindowRect)(wp.hwnd, &r);
			wp.cx = r.right - r.left;
			wp.cy = r.bottom - r.top;
		}

		if (wp.cx > mr.right - mr.left)
		{
			wp.cx = mr.right - mr.left;
			wp.flags &= ~SWP_NOSIZE;
		}

		if (wp.x + wp.cx > mr.right)
		{
			HWND parent = GetNextWindow(wp.hwnd, GW_HWNDNEXT);
			while (Gdi::GuiThread::isGuiThreadWindow(parent))
			{
				parent = GetNextWindow(parent, GW_HWNDNEXT);
			}
			ATOM atom = parent ? static_cast<ATOM>(GetClassLong(parent, GCW_ATOM)) : 0;
			if (Gdi::MENU_ATOM == atom)
			{
				RECT parentRect = {};
				CALL_ORIG_FUNC(GetWindowRect)(parent, &parentRect);
				wp.x = std::max<int>(parentRect.left + 3 - wp.cx, 0);
			}
			else
			{
				wp.x = mr.right - wp.cx;
			}
		}

		if (wp.y + wp.cy > mr.bottom)
		{
			wp.y = mr.bottom - wp.cy;
		}
	}

	void hookUser32WndProc(User32WndProc& user32WndProc, WNDPROC newWndProc,
		const std::string& procName, const std::string& className, bool isUnicode)
	{
		WNDPROC wndProc = nullptr;
		if (isUnicode)
		{
			WNDCLASSW wc = {};
			GetClassInfoW(nullptr, std::wstring(className.begin(), className.end()).c_str(), &wc);
			wndProc = wc.lpfnWndProc;
		}
		else
		{
			WNDCLASSA wc = {};
			GetClassInfoA(nullptr, className.c_str(), &wc);
			wndProc = wc.lpfnWndProc;
		}

		HMODULE module = Compat::getModuleHandleFromAddress(wndProc);
		if (module != GetModuleHandle("ntdll") && module != GetModuleHandle("user32"))
		{
			LOG_INFO << "Failed to hook a user32 window procedure: " << className;
			return;
		}

		user32WndProc.oldWndProc = wndProc;
		user32WndProc.oldWndProcTrampoline = wndProc;
		user32WndProc.newWndProc = newWndProc;
		user32WndProc.procName = procName;
		user32WndProc.className = className;
		user32WndProc.isUnicode = isUnicode;

		Compat::hookFunction(reinterpret_cast<void*&>(user32WndProc.oldWndProcTrampoline),
			user32WndProc.newWndProc, procName.c_str());
	}

	template <WndProcHook wndProcHook>
	void hookUser32WndProcA(const std::string& className, const std::string& procName)
	{
		hookUser32WndProc(g_user32WndProcA<wndProcHook>, user32WndProcA<wndProcHook>,
			procName + 'A', className, false);
	}

	template <WndProcHook wndProcHook>
	void hookUser32WndProcW(const std::string& className, const std::string& procName)
	{
		hookUser32WndProc(g_user32WndProcW<wndProcHook>, user32WndProcW<wndProcHook>,
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
		case WM_WINDOWPOSCHANGING:
		{
			LRESULT result = CallWindowProc(origWndProc, hwnd, msg, wParam, lParam);
			auto& wp = *reinterpret_cast<WINDOWPOS*>(lParam);
			if (!(wp.flags & SWP_NOMOVE))
			{
				fixPopupMenuPosition(wp);
			}
			return result;
		}

		case WM_WINDOWPOSCHANGED:
		{
			LRESULT result = CallWindowProc(origWndProc, hwnd, msg, wParam, lParam);
			auto exStyle = CALL_ORIG_FUNC(GetWindowLongA)(hwnd, GWL_EXSTYLE);
			if (exStyle & WS_EX_LAYERED)
			{
				CALL_ORIG_FUNC(SetWindowLongA)(hwnd, GWL_EXSTYLE, exStyle & ~WS_EX_LAYERED);
			}
			RedrawWindow(hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_UPDATENOW);
			DDraw::RealPrimarySurface::scheduleUpdate();
			return result;
		}

		default:
			DDraw::RealPrimarySurface::scheduleUpdate();
			return CallWindowProc(origWndProc, hwnd, msg, wParam, lParam);
		}
	}

	LRESULT onEraseBackground(HWND hwnd, HDC dc, WNDPROC origWndProc)
	{
		return CallWindowProc(origWndProc, hwnd, WM_ERASEBKGND,
			reinterpret_cast<WPARAM>(static_cast<HDC>(Gdi::CompatDc(dc))), 0);
	}

	LRESULT onNcActivate(HWND hwnd, WPARAM /*wParam*/, LPARAM lParam)
	{
		if (-1 != lParam)
		{
			RECT r = { -1, -1, 0, 0 };
			RedrawWindow(hwnd, &r, nullptr, RDW_INVALIDATE | RDW_FRAME);
		}
		return TRUE;
	}

	LRESULT onNcPaint(HWND hwnd, WNDPROC origWndProc)
	{
		D3dDdi::ScopedCriticalSection lock;
		HDC windowDc = GetWindowDC(hwnd);
		CallWindowProc(origWndProc, hwnd, WM_PRINT,
			reinterpret_cast<WPARAM>(static_cast<HDC>(Gdi::CompatDc(windowDc))), PRF_NONCLIENT);
		Gdi::TitleBar(hwnd).drawAll(windowDc);
		ReleaseDC(hwnd, windowDc);
		return 0;
	}

	LRESULT onPaint(HWND hwnd, WNDPROC origWndProc)
	{
		PAINTSTRUCT paint = {};
		HDC dc = BeginPaint(hwnd, &paint);
		CallWindowProc(origWndProc, hwnd, WM_PRINTCLIENT,
			reinterpret_cast<WPARAM>(static_cast<HDC>(Gdi::CompatDc(dc))), PRF_CLIENT);
		EndPaint(hwnd, &paint);
		return 0;
	}

	LRESULT onPrint(HWND hwnd, UINT msg, HDC dc, LONG flags, WNDPROC origWndProc)
	{
		return CallWindowProc(origWndProc, hwnd, msg,
			reinterpret_cast<WPARAM>(static_cast<HDC>(Gdi::CompatDc(dc))), flags);
	}

	LRESULT onSetText(HWND hwnd, WPARAM wParam, LPARAM lParam, WNDPROC origWndProc)
	{
		LRESULT result = CallWindowProc(origWndProc, hwnd, WM_SETTEXT, wParam, lParam);
		if (TRUE == result && (CALL_ORIG_FUNC(GetWindowLongA)(hwnd, GWL_STYLE) & WS_CAPTION))
		{
			RECT r = { -1, -1, 0, 0 };
			RedrawWindow(hwnd, &r, nullptr, RDW_INVALIDATE | RDW_FRAME);
		}
		return result;
	}

	LRESULT scrollBarWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, WNDPROC origWndProc)
	{
		switch (msg)
		{
		case WM_LBUTTONDOWN:
		{
			Gdi::ScrollBar sb(hwnd, SB_CTL);
			sb.onLButtonDown(lParam);
			return CallWindowProc(origWndProc, hwnd, msg, wParam, lParam);
		}

		case WM_PAINT:
			return onPaint(hwnd, origWndProc);

		case WM_SETCURSOR:
			if (CALL_ORIG_FUNC(GetWindowLongA)(hwnd, GWL_STYLE) & (SBS_SIZEBOX | SBS_SIZEGRIP))
			{
				SetCursor(CALL_ORIG_FUNC(LoadCursorA)(nullptr, IDC_SIZENWSE));
			}
			return TRUE;
		}

		return CallWindowProc(origWndProc, hwnd, msg, wParam, lParam);
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
		LOG_FUNC(procName.c_str(), Compat::WindowMessageStruct(hwnd, uMsg, wParam, lParam));
		LRESULT result = wndProcHook(hwnd, uMsg, wParam, lParam, oldWndProcTrampoline);
		if (WM_CREATE == uMsg && -1 != result ||
			WM_NCCREATE == uMsg && result)
		{
			Gdi::WinProc::onCreateWindow(hwnd);
		}
		return LOG_RESULT(result);
	}

	template <WndProcHook wndProcHook>
	LRESULT CALLBACK user32WndProcA(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
	{
		auto& wp = g_user32WndProcA<wndProcHook>;
		return user32WndProc(hwnd, uMsg, wParam, lParam, wp.procName, wndProcHook, wp.oldWndProcTrampoline);
	}

	template <WndProcHook wndProcHook>
	LRESULT CALLBACK user32WndProcW(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
	{
		auto& wp = g_user32WndProcW<wndProcHook>;
		return user32WndProc(hwnd, uMsg, wParam, lParam, wp.procName, wndProcHook, wp.oldWndProcTrampoline);
	}
}

#define HOOK_USER32_WNDPROC(className, wndProcHook) hookUser32WndProc<wndProcHook>(className, #wndProcHook)
#define HOOK_USER32_WNDPROCW(className, wndProcHook) hookUser32WndProcW<wndProcHook>(className, #wndProcHook)

namespace Gdi
{
	namespace User32WndProcs
	{
		void installHooks()
		{
			HOOK_USER32_WNDPROC("Button", buttonWndProc);
			HOOK_USER32_WNDPROC("ComboBox", comboBoxWndProc);
			HOOK_USER32_WNDPROC("ComboLBox", comboListBoxWndProc);
			HOOK_USER32_WNDPROC("Edit", editWndProc);
			HOOK_USER32_WNDPROC("ListBox", listBoxWndProc);
			HOOK_USER32_WNDPROC("MDIClient", mdiClientWndProc);
			HOOK_USER32_WNDPROC("ScrollBar", scrollBarWndProc);
			HOOK_USER32_WNDPROC("Static", staticWndProc);
			HOOK_USER32_WNDPROC("#32768", menuWndProc);

			HOOK_FUNCTION(user32, DefDlgProcA, defDlgProcA);
			HOOK_FUNCTION(user32, DefDlgProcW, defDlgProcW);
			HOOK_FUNCTION(user32, DefWindowProcA, defWindowProcA);
			HOOK_FUNCTION(user32, DefWindowProcW, defWindowProcW);
			HOOK_FUNCTION(user32, SetMenuItemInfoW, setMenuItemInfoW);
		}
	}
}
