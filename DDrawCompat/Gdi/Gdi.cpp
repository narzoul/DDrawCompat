#include <dwmapi.h>

#include <Common/Hook.h>
#include <Config/Settings/GdiInterops.h>
#include <DDraw/RealPrimarySurface.h>
#include <DDraw/Surfaces/PrimarySurface.h>
#include <Gdi/Caret.h>
#include <Gdi/Cursor.h>
#include <Gdi/Dc.h>
#include <Gdi/DcFunctions.h>
#include <Gdi/Font.h>
#include <Gdi/Gdi.h>
#include <Gdi/GuiThread.h>
#include <Gdi/Icon.h>
#include <Gdi/Metrics.h>
#include <Gdi/Palette.h>
#include <Gdi/PresentationWindow.h>
#include <Gdi/ScrollFunctions.h>
#include <Gdi/User32WndProcs.h>
#include <Gdi/WinProc.h>

namespace
{
	ATOM g_autoSuggestDropdownAtom = 0;
	ATOM g_comboLBoxAtom = 0;
	ATOM g_microsoftWindowsTooltipAtom = 0;
	ATOM g_sysShadowAtom = 0;
	ATOM g_tooltips_class32Atom = 0;

	const std::map<std::wstring, ATOM&> g_classAtoms = {
		{ L"Auto-Suggest Dropdown", g_autoSuggestDropdownAtom },
		{ L"ComboLBox", g_comboLBoxAtom },
		{ L"MicrosoftWindowsTooltip", g_microsoftWindowsTooltipAtom },
		{ L"SysShadow", g_sysShadowAtom },
		{ L"tooltips_class32", g_tooltips_class32Atom }
	};

	HRESULT WINAPI dwmEnableComposition([[maybe_unused]] UINT uCompositionAction)
	{
		LOG_FUNC("DwmEnableComposition", uCompositionAction);
		return LOG_RESULT(0);
	}

	BOOL WINAPI getDeviceGammaRamp(HDC hdc, LPVOID lpRamp)
	{
		LOG_FUNC("GetDeviceGammaRamp", hdc, lpRamp);
		if (!CALL_ORIG_FUNC(GetDeviceGammaRamp)(hdc, lpRamp))
		{
			return LOG_RESULT(FALSE);
		}

		auto p = static_cast<WORD*>(lpRamp);
		if (Config::logLevel.get() >= Config::Settings::LogLevel::DEBUG)
		{
			LOG_DEBUG << "Original gamma ramp (red):   " << Compat::array(p, 256);
			LOG_DEBUG << "Original gamma ramp (green): " << Compat::array(p + 256, 256);
			LOG_DEBUG << "Original gamma ramp (blue):  " << Compat::array(p + 512, 256);
		}

		for (unsigned i = 0; i < 3; ++i)
		{
			for (unsigned k = 0; k < 256; ++k)
			{
				*p = static_cast<WORD>(k * 257);
				++p;
			}
		}
		return LOG_RESULT(TRUE);
	}
}

namespace Gdi
{
	void checkDesktopComposition()
	{
		BOOL isEnabled = FALSE;
		HRESULT result = DwmIsCompositionEnabled(&isEnabled);
		LOG_DEBUG << "DwmIsCompositionEnabled: " << Compat::hex(result) << " " << isEnabled;
		if (!isEnabled)
		{
			LOG_ONCE("Warning: Desktop composition is disabled. This is not supported.");
		}
	}

	void dllThreadDetach()
	{
		WinProc::dllThreadDetach();
		Dc::dllThreadDetach();
	}

	ATOM getClassAtom(const std::wstring& className)
	{
		WNDCLASSW wc = {};
		return static_cast<ATOM>(GetClassInfoW(nullptr, className.c_str(), &wc));
	}

	ATOM getComboLBoxAtom()
	{
		return g_comboLBoxAtom;
	}

	ATOM getSysShadowAtom()
	{
		return g_sysShadowAtom;
	}

	void installHooks()
	{
		for (const auto& atom : g_classAtoms)
		{
			atom.second = getClassAtom(atom.first.c_str());
		}

#pragma warning (disable : 4995)
		HOOK_FUNCTION(dwmapi, DwmEnableComposition, dwmEnableComposition);
#pragma warning (default : 4995)
		HOOK_FUNCTION(gdi32, GetDeviceGammaRamp, getDeviceGammaRamp);

		checkDesktopComposition();
		DisableProcessWindowsGhosting();

		DcFunctions::installHooks();
		Icon::installHooks();
		Metrics::installHooks();
		Palette::installHooks();
		PresentationWindow::installHooks();
		ScrollFunctions::installHooks();
		User32WndProcs::installHooks();
		Caret::installHooks();
		Cursor::installHooks();
		Font::installHooks();
		WinProc::installHooks();
		GuiThread::installHooks();
	}

	bool isDisplayDc(HDC dc)
	{
		return dc && OBJ_DC == GetObjectType(dc) && DT_RASDISPLAY == CALL_ORIG_FUNC(GetDeviceCaps)(dc, TECHNOLOGY);
	}

	bool isRedirected(HWND hwnd)
	{
		const HWND root = GetAncestor(hwnd, GA_ROOT);
		const DWORD rootAtom = root ? GetClassLongA(root, GCW_ATOM) : MAXDWORD;
		if (!root && !Config::gdiInterops.get().desktop ||
			DIALOG_ATOM == rootAtom && !Config::gdiInterops.get().dialogs ||
			!Config::gdiInterops.get().windows ||
			MENU_ATOM == rootAtom ||
			g_autoSuggestDropdownAtom == rootAtom && !isRedirected(GetParent(hwnd)) ||
			g_comboLBoxAtom == rootAtom && !GetPropA(root, "DDCRedirected") ||
			g_microsoftWindowsTooltipAtom == rootAtom ||
			g_tooltips_class32Atom == rootAtom ||
			(CALL_ORIG_FUNC(GetWindowLongA)(root, GWL_EXSTYLE) & WS_EX_LAYERED))
		{
			return false;
		}
		return true;
	}

	bool isRedirected(HDC dc)
	{
		if (!Config::gdiInterops.anyRedirects() || !isDisplayDc(dc))
		{
			return false;
		}
		const HWND hwnd = CALL_ORIG_FUNC(WindowFromDC)(dc);
		return hwnd ? Gdi::isRedirected(hwnd) : Config::gdiInterops.get().desktop;
	}

	void onRegisterClass(const std::wstring& className, ATOM atom)
	{
		auto it = g_classAtoms.find(className);
		if (it != g_classAtoms.end())
		{
			it->second = atom;
		}
	}
}
