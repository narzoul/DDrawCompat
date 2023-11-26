#include <Common/Hook.h>
#include <Common/Log.h>
#include <Gdi/CompatDc.h>
#include <Gdi/Dc.h>
#include <Gdi/DcFunctions.h>
#include <Gdi/Font.h>
#include <Gdi/Gdi.h>
#include <Gdi/Region.h>
#include <Gdi/VirtualScreen.h>
#include <Win32/DisplayMode.h>

namespace
{
	template <typename Char>
	void logString(Compat::Log& log, const Char* str, int length)
	{
		log << '"';
		if (length < 0)
		{
			log << str;
		}
		else
		{
			for (int i = 0; i < length; ++i)
			{
				log << static_cast<char>(str[i]);
			}
		}
		log << '"';
	}

	template <typename Char>
	void logExtTextOutString(Compat::Log& log, UINT options, const Char* lpString, UINT c)
	{
		if (options & ETO_GLYPH_INDEX)
		{
			log << static_cast<const void*>(lpString);
		}
		else
		{
			logString(log, lpString, c);
		}
	}
}

namespace Compat
{
	template <>
	void LogParam<&DrawEscape, 3>::log(Log& log, HDC, int, int, LPCSTR lpIn)
	{
		log << static_cast<const void*>(lpIn);
	}

	template <>
	void LogParam<&DrawTextA, 1>::log(Log& log, HDC, LPCSTR lpchText, int cchText, LPRECT, UINT)
	{
		logString(log, lpchText, cchText);
	}

	template <>
	void LogParam<&DrawTextW, 1>::log(Log& log, HDC, LPCWSTR lpchText, int cchText, LPRECT, UINT)
	{
		logString(log, lpchText, cchText);
	}

	template <>
	void LogParam<&DrawTextExA, 1>::log(Log& log, HDC, LPSTR lpchText, int cchText, LPRECT, UINT, LPDRAWTEXTPARAMS)
	{
		logString(log, lpchText, cchText);
	}

	template <>
	void LogParam<&DrawTextExW, 1>::log(Log& log, HDC, LPWSTR lpchText, int cchText, LPRECT, UINT, LPDRAWTEXTPARAMS)
	{
		logString(log, lpchText, cchText);
	}

	template <>
	void LogParam<&ExtTextOutA, 5>::log(Log& log, HDC, int, int, UINT options, const RECT*, LPCSTR lpString, UINT c, const INT*)
	{
		logExtTextOutString(log, options, lpString, c);
	}

	template <>
	void LogParam<&ExtTextOutW, 5>::log(Log& log, HDC, int, int, UINT options, const RECT*, LPCWSTR lpString, UINT c, const INT*)
	{
		logExtTextOutString(log, options, lpString, c);
	}

	template <>
	void LogParam<&TabbedTextOutA, 3>::log(Log& log, HDC, int, int, LPCSTR lpString, int chCount, int, const INT*, int)
	{
		logString(log, lpString, chCount);
	}

	template <>
	void LogParam<&TabbedTextOutW, 3>::log(Log& log, HDC, int, int, LPCWSTR lpString, int chCount, int, const INT*, int)
	{
		logString(log, lpString, chCount);
	}

	template <>
	void LogParam<&TextOutA, 3>::log(Log& log, HDC, int, int, LPCSTR lpString, int c)
	{
		logString(log, lpString, c);
	}

	template <>
	void LogParam<&TextOutW, 3>::log(Log& log, HDC, int, int, LPCWSTR lpString, int c)
	{
		logString(log, lpString, c);
	}
}

namespace
{
	template <auto func>
	const char* g_funcName = nullptr;

	thread_local UINT g_disableDibRedirection = 0;

#define CREATE_DC_FUNC_ATTRIBUTE(attribute) \
	template <auto origFunc> \
	bool attribute() \
	{ \
		return false; \
	}

#define SET_DC_FUNC_ATTRIBUTE(attribute, func) \
	template <> \
	bool attribute<&func>() \
	{ \
		return true; \
	}

#define SET_TEXT_DC_FUNC_ATTRIBUTE(attribute, func) \
	SET_DC_FUNC_ATTRIBUTE(attribute, func##A) \
	SET_DC_FUNC_ATTRIBUTE(attribute, func##W)

	CREATE_DC_FUNC_ATTRIBUTE(isPositionUpdated);
	SET_DC_FUNC_ATTRIBUTE(isPositionUpdated, AngleArc);
	SET_DC_FUNC_ATTRIBUTE(isPositionUpdated, ArcTo);
	SET_DC_FUNC_ATTRIBUTE(isPositionUpdated, LineTo);
	SET_DC_FUNC_ATTRIBUTE(isPositionUpdated, PolyBezierTo);
	SET_DC_FUNC_ATTRIBUTE(isPositionUpdated, PolyDraw);
	SET_DC_FUNC_ATTRIBUTE(isPositionUpdated, PolylineTo);
	SET_TEXT_DC_FUNC_ATTRIBUTE(isPositionUpdated, ExtTextOut);
	SET_TEXT_DC_FUNC_ATTRIBUTE(isPositionUpdated, PolyTextOut);
	SET_TEXT_DC_FUNC_ATTRIBUTE(isPositionUpdated, TabbedTextOut);
	SET_TEXT_DC_FUNC_ATTRIBUTE(isPositionUpdated, TextOut);

	CREATE_DC_FUNC_ATTRIBUTE(isReadOnly);
	SET_DC_FUNC_ATTRIBUTE(isReadOnly, GetDIBits);
	SET_DC_FUNC_ATTRIBUTE(isReadOnly, GetPixel);

	BOOL WINAPI GdiDrawStream(HDC, DWORD, DWORD) { return FALSE; }
	BOOL WINAPI PolyPatBlt(HDC, DWORD, DWORD, DWORD, DWORD) { return FALSE; }

	bool hasDisplayDcArg(HDC dc)
	{
		return Gdi::isDisplayDc(dc);
	}

	template <typename T>
	bool hasDisplayDcArg(T)
	{
		return false;
	}

	template <typename T, typename... Params>
	bool hasDisplayDcArg(T t, Params... params)
	{
		return hasDisplayDcArg(t) || hasDisplayDcArg(params...);
	}

	bool lpToScreen(HWND hwnd, HDC dc, POINT& p)
	{
		LPtoDP(dc, &p, 1);
		RECT wr = {};
		GetWindowRect(hwnd, &wr);
		p.x += wr.left;
		p.y += wr.top;
		return true;
	}

	template <typename T>
	T replaceDc(T t)
	{
		return t;
	}

	Gdi::CompatDc replaceDc(HDC dc)
	{
		return Gdi::CompatDc(dc);
	}

	template <auto origFunc, typename Result, typename... Params>
	Result WINAPI compatGdiDcFunc(HDC hdc, Params... params)
	{
		LOG_FUNC_CUSTOM(origFunc, g_funcName<origFunc>, hdc, params...);

		if (hasDisplayDcArg(hdc, params...))
		{
			Gdi::CompatDc compatDc(hdc, isReadOnly<origFunc>());
			Result result = Compat::g_origFuncPtr<origFunc>(compatDc, replaceDc(params)...);
			if (isPositionUpdated<origFunc>() && result)
			{
				POINT currentPos = {};
				GetCurrentPositionEx(compatDc, &currentPos);
				MoveToEx(hdc, currentPos.x, currentPos.y, nullptr);
			}
			return LOG_RESULT(result);
		}

		return LOG_RESULT(Compat::g_origFuncPtr<origFunc>(hdc, params...));
	}

	template <>
	BOOL WINAPI compatGdiDcFunc<&ExtTextOutW>(
		HDC hdc, int x, int y, UINT options, const RECT* lprect, LPCWSTR lpString, UINT c, const INT* lpDx)
	{
		LOG_FUNC_CUSTOM(&ExtTextOutW, "ExtTextOutW", hdc, x, y, options, lprect, lpString, c, lpDx);

		if (hasDisplayDcArg(hdc))
		{
			HWND hwnd = CALL_ORIG_FUNC(WindowFromDC)(hdc);
			ATOM atom = static_cast<ATOM>(GetClassLong(hwnd, GCW_ATOM));
			POINT p = { x, y };
			if (Gdi::MENU_ATOM == atom)
			{
				RedrawWindow(hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE);
			}
			else if (GetCurrentThreadId() == GetWindowThreadProcessId(hwnd, nullptr) &&
				lpToScreen(hwnd, hdc, p) &&
				HTMENU == SendMessage(hwnd, WM_NCHITTEST, 0, (p.y << 16) | (p.x & 0xFFFF)))
			{
				CALL_ORIG_FUNC(SetWindowPos)(hwnd, nullptr, 0, 0, 0, 0, SWP_NOSIZE | SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED | SWP_NOSENDCHANGING);
			}
			else
			{
				Gdi::CompatDc compatDc(hdc);
				BOOL result = CALL_ORIG_FUNC(ExtTextOutW)(compatDc, x, y, options, lprect, lpString, c, lpDx);
				if (result)
				{
					POINT currentPos = {};
					GetCurrentPositionEx(compatDc, &currentPos);
					MoveToEx(hdc, currentPos.x, currentPos.y, nullptr);
				}
				return LOG_RESULT(result);
			}
		}
		else
		{
			return LOG_RESULT(CALL_ORIG_FUNC(ExtTextOutW)(hdc, x, y, options, lprect, lpString, c, lpDx));
		}

		return LOG_RESULT(TRUE);
	}

	template <auto origFunc, typename Result, typename... Params>
	Result WINAPI compatGdiTextDcFunc(HDC dc, Params... params)
	{
		Gdi::Font::Mapper fontMapper(dc);
		return compatGdiDcFunc<origFunc, Result>(dc, params...);
	}

	HBITMAP WINAPI createBitmap(int nWidth, int nHeight, UINT nPlanes, UINT nBitCount, const VOID* lpBits)
	{
		LOG_FUNC("CreateBitmap", nWidth, nHeight, nPlanes, nBitCount, lpBits);
		if (!g_disableDibRedirection && nWidth > 0 && nHeight > 0 && 1 == nPlanes && nBitCount >= 8)
		{
			HBITMAP bmp = Gdi::VirtualScreen::createOffScreenDib(nWidth, -nHeight, nBitCount);
			if (bmp && lpBits)
			{
				SetBitmapBits(bmp, (nWidth * nBitCount + 15) / 16 * 2 * nHeight, lpBits);
			}
			return LOG_RESULT(bmp);
		}
		return LOG_RESULT(CALL_ORIG_FUNC(CreateBitmap)(nWidth, nHeight, nPlanes, nBitCount, lpBits));
	}

	HBITMAP WINAPI createBitmapIndirect(const BITMAP* pbm)
	{
		LOG_FUNC("CreateBitmapIndirect", pbm);
		return LOG_RESULT(createBitmap(pbm->bmWidth, pbm->bmHeight, pbm->bmPlanes, pbm->bmBitsPixel, pbm->bmBits));
	}

	HBITMAP WINAPI createCompatibleBitmap(HDC hdc, int cx, int cy)
	{
		LOG_FUNC("CreateCompatibleBitmap", hdc, cx, cy);
		if (!g_disableDibRedirection && cx > 0 && cy > 0 && Gdi::isDisplayDc(hdc))
		{
			return LOG_RESULT(Gdi::VirtualScreen::createOffScreenDib(cx, -cy, Win32::DisplayMode::getBpp()));
		}
		return LOG_RESULT(CALL_ORIG_FUNC(CreateCompatibleBitmap)(hdc, cx, cy));
	}

	HBITMAP WINAPI createDIBitmap(HDC hdc, const BITMAPINFOHEADER* lpbmih, DWORD fdwInit,
		const void* lpbInit, const BITMAPINFO* lpbmi, UINT fuUsage)
	{
		LOG_FUNC("CreateDIBitmap", hdc, lpbmih, fdwInit, lpbInit, lpbmi, fuUsage);
		if (!g_disableDibRedirection && lpbmih && Gdi::isDisplayDc(hdc))
		{
			HBITMAP bitmap = Gdi::VirtualScreen::createOffScreenDib(
				lpbmih->biWidth, lpbmih->biHeight, Win32::DisplayMode::getBpp());
			if ((fdwInit & CBM_INIT) && bitmap && lpbInit && lpbmi)
			{
				SetDIBits(hdc, bitmap, 0, std::abs(lpbmi->bmiHeader.biHeight), lpbInit, lpbmi, fuUsage);
			}
			return LOG_RESULT(bitmap);
		}
		return LOG_RESULT(CALL_ORIG_FUNC(CreateDIBitmap)(hdc, lpbmih, fdwInit, lpbInit, lpbmi, fuUsage));
	}

	HBITMAP WINAPI createDiscardableBitmap(HDC hdc, int nWidth, int nHeight)
	{
		LOG_FUNC("CreateDiscardableBitmap", hdc, nWidth, nHeight);
		return LOG_RESULT(CALL_ORIG_FUNC(createCompatibleBitmap)(hdc, nWidth, nHeight));
	}

	BOOL WINAPI drawCaption(HWND hwnd, HDC hdc, const RECT* lprect, UINT flags)
	{
		LOG_FUNC("DrawCaption", hwnd, hdc, lprect, flags);
		if (Gdi::isDisplayDc(hdc))
		{
			return LOG_RESULT(CALL_ORIG_FUNC(DrawCaption)(hwnd, Gdi::CompatDc(hdc), lprect, flags));
		}
		return LOG_RESULT(CALL_ORIG_FUNC(DrawCaption)(hwnd, hdc, lprect, flags));
	}

	template <auto origFunc>
	void hookGdiDcFunction(const char* moduleName, const char* funcName)
	{
		g_funcName<origFunc> = funcName;
		Compat::hookFunction<origFunc>(moduleName, funcName, &compatGdiDcFunc<origFunc>);
	}

	template <auto origFunc>
	void hookGdiTextDcFunction(const char* moduleName, const char* funcName)
	{
		g_funcName<origFunc> = funcName;
		Compat::hookFunction<origFunc>(moduleName, funcName, &compatGdiTextDcFunc<origFunc>);
	}

	HWND WINAPI windowFromDc(HDC dc)
	{
		return CALL_ORIG_FUNC(WindowFromDC)(Gdi::Dc::getOrigDc(dc));
	}
}

#define HOOK_GDI_DC_FUNCTION(module, func) \
	hookGdiDcFunction<&func>(#module, #func)

#define HOOK_GDI_TEXT_DC_FUNCTION(module, func) \
	hookGdiTextDcFunction<&func##A>(#module, #func"A"); \
	hookGdiTextDcFunction<&func##W>(#module, #func"W")

namespace Gdi
{
	namespace DcFunctions
	{
		void disableDibRedirection(bool disable)
		{
			g_disableDibRedirection += disable ? 1 : -1;
		}

		void installHooks()
		{
			// Bitmap functions
			HOOK_GDI_DC_FUNCTION(msimg32, AlphaBlend);
			HOOK_GDI_DC_FUNCTION(gdi32, BitBlt);
			HOOK_FUNCTION(gdi32, CreateBitmap, createBitmap);
			HOOK_FUNCTION(gdi32, CreateBitmapIndirect, createBitmapIndirect);
			HOOK_FUNCTION(gdi32, CreateCompatibleBitmap, createCompatibleBitmap);
			HOOK_FUNCTION(gdi32, CreateDIBitmap, createDIBitmap);
			HOOK_FUNCTION(gdi32, CreateDiscardableBitmap, createDiscardableBitmap);
			HOOK_GDI_DC_FUNCTION(gdi32, ExtFloodFill);
			HOOK_GDI_DC_FUNCTION(gdi32, GdiAlphaBlend);
			HOOK_GDI_DC_FUNCTION(gdi32, GdiGradientFill);
			HOOK_GDI_DC_FUNCTION(gdi32, GdiTransparentBlt);
			HOOK_GDI_DC_FUNCTION(gdi32, GetPixel);
			HOOK_GDI_DC_FUNCTION(msimg32, GradientFill);
			HOOK_GDI_DC_FUNCTION(gdi32, MaskBlt);
			HOOK_GDI_DC_FUNCTION(gdi32, PlgBlt);
			HOOK_GDI_DC_FUNCTION(gdi32, SetDIBitsToDevice);
			HOOK_GDI_DC_FUNCTION(gdi32, SetPixel);
			HOOK_GDI_DC_FUNCTION(gdi32, SetPixelV);
			HOOK_GDI_DC_FUNCTION(gdi32, StretchBlt);
			HOOK_GDI_DC_FUNCTION(gdi32, StretchDIBits);
			HOOK_GDI_DC_FUNCTION(msimg32, TransparentBlt);

			// Brush functions
			HOOK_GDI_DC_FUNCTION(gdi32, PatBlt);

			// Device context functions
			HOOK_GDI_DC_FUNCTION(gdi32, DrawEscape);
			HOOK_FUNCTION(user32, WindowFromDC, windowFromDc);

			// Filled shape functions
			HOOK_GDI_DC_FUNCTION(gdi32, Chord);
			HOOK_GDI_DC_FUNCTION(gdi32, Ellipse);
			HOOK_GDI_DC_FUNCTION(user32, FillRect);
			HOOK_GDI_DC_FUNCTION(user32, FrameRect);
			HOOK_GDI_DC_FUNCTION(user32, InvertRect);
			HOOK_GDI_DC_FUNCTION(gdi32, Pie);
			HOOK_GDI_DC_FUNCTION(gdi32, Polygon);
			HOOK_GDI_DC_FUNCTION(gdi32, PolyPolygon);
			HOOK_GDI_DC_FUNCTION(gdi32, Rectangle);
			HOOK_GDI_DC_FUNCTION(gdi32, RoundRect);

			// Font and text functions
			HOOK_GDI_TEXT_DC_FUNCTION(user32, DrawText);
			HOOK_GDI_TEXT_DC_FUNCTION(user32, DrawTextEx);
			HOOK_GDI_TEXT_DC_FUNCTION(gdi32, ExtTextOut);
			HOOK_GDI_TEXT_DC_FUNCTION(gdi32, PolyTextOut);
			HOOK_GDI_TEXT_DC_FUNCTION(user32, TabbedTextOut);
			HOOK_GDI_TEXT_DC_FUNCTION(gdi32, TextOut);

			// Icon functions
			HOOK_GDI_DC_FUNCTION(user32, DrawIcon);
			HOOK_GDI_DC_FUNCTION(user32, DrawIconEx);

			// Line and curve functions
			HOOK_GDI_DC_FUNCTION(gdi32, AngleArc);
			HOOK_GDI_DC_FUNCTION(gdi32, Arc);
			HOOK_GDI_DC_FUNCTION(gdi32, ArcTo);
			HOOK_GDI_DC_FUNCTION(gdi32, LineTo);
			HOOK_GDI_DC_FUNCTION(gdi32, PolyBezier);
			HOOK_GDI_DC_FUNCTION(gdi32, PolyBezierTo);
			HOOK_GDI_DC_FUNCTION(gdi32, PolyDraw);
			HOOK_GDI_DC_FUNCTION(gdi32, Polyline);
			HOOK_GDI_DC_FUNCTION(gdi32, PolylineTo);
			HOOK_GDI_DC_FUNCTION(gdi32, PolyPolyline);

			// Painting and drawing functions
			HOOK_FUNCTION(user32, DrawCaption, drawCaption);
			HOOK_GDI_DC_FUNCTION(user32, DrawEdge);
			HOOK_GDI_DC_FUNCTION(user32, DrawFocusRect);
			HOOK_GDI_DC_FUNCTION(user32, DrawFrameControl);
			HOOK_GDI_TEXT_DC_FUNCTION(user32, DrawState);
			HOOK_GDI_TEXT_DC_FUNCTION(user32, GrayString);
			HOOK_GDI_DC_FUNCTION(user32, PaintDesktop);

			// Region functions
			HOOK_GDI_DC_FUNCTION(gdi32, FillRgn);
			HOOK_GDI_DC_FUNCTION(gdi32, FrameRgn);
			HOOK_GDI_DC_FUNCTION(gdi32, InvertRgn);
			HOOK_GDI_DC_FUNCTION(gdi32, PaintRgn);

			// Scroll bar functions
			HOOK_GDI_DC_FUNCTION(user32, ScrollDC);

			// Undocumented functions
			HOOK_GDI_DC_FUNCTION(gdi32, GdiDrawStream);
			HOOK_GDI_DC_FUNCTION(gdi32, PolyPatBlt);
		}
	}
}
