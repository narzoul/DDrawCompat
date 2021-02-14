#include <map>

#include <Common/Hook.h>
#include <Common/Log.h>
#include <Gdi/DcFunctions.h>
#include <Gdi/Icon.h>

namespace
{
	std::map<void*, const char*> g_funcNames;

	template <typename Result, typename... Params>
	using FuncPtr = Result(WINAPI*)(Params...);

	template <typename OrigFuncPtr, OrigFuncPtr origFunc, typename... Params>
	struct EnableDibRedirection
	{
		bool operator()(Params...) { return false; }
	};

	template <typename... Params>
	struct EnableDibRedirection<decltype(&CopyImage), &CopyImage, Params...>
	{
		bool operator()(HANDLE, UINT type, int, int, UINT)
		{
			return IMAGE_CURSOR != type && IMAGE_ICON != type;
		}
	};

	struct EnableDibRedirectionLoadImage
	{
		template <typename String>
		bool operator()(HINSTANCE, String, UINT type, int, int, UINT)
		{
			return IMAGE_CURSOR != type && IMAGE_ICON != type;
		}
	};

	template <typename... Params>
	struct EnableDibRedirection<decltype(&LoadImageA), &LoadImageA, Params...> : EnableDibRedirectionLoadImage {};
	template <typename... Params>
	struct EnableDibRedirection<decltype(&LoadImageW), &LoadImageW, Params...> : EnableDibRedirectionLoadImage {};

	template <typename OrigFuncPtr, OrigFuncPtr origFunc, typename Result, typename... Params>
	Result WINAPI iconFunc(Params... params)
	{
		LOG_FUNC(g_funcNames[origFunc], params...);

		if (EnableDibRedirection<OrigFuncPtr, origFunc, Params...>()(params...))
		{
			return LOG_RESULT(Compat::getOrigFuncPtr<OrigFuncPtr, origFunc>()(params...));
		}

		Gdi::DcFunctions::disableDibRedirection(true);
		Result result = Compat::getOrigFuncPtr<OrigFuncPtr, origFunc>()(params...);
		Gdi::DcFunctions::disableDibRedirection(false);
		return LOG_RESULT(result);
	}

	template <typename OrigFuncPtr, OrigFuncPtr origFunc, typename Result, typename... Params>
	OrigFuncPtr getIconFuncPtr(FuncPtr<Result, Params...>)
	{
		return &iconFunc<OrigFuncPtr, origFunc, Result, Params...>;
	}

	template <typename OrigFuncPtr, OrigFuncPtr origFunc>
	void hookIconFunc(const char* moduleName, const char* funcName)
	{
#ifdef DEBUGLOGS
		g_funcNames[origFunc] = funcName;
#endif

		Compat::hookFunction<OrigFuncPtr, origFunc>(
			moduleName, funcName, getIconFuncPtr<OrigFuncPtr, origFunc>(origFunc));
	}

	template <typename WndClass, typename WndClassEx>
	ATOM registerClass(const WndClass* lpWndClass, ATOM(WINAPI* origRegisterClassEx)(const WndClassEx*))
	{
		if (!lpWndClass)
		{
			return origRegisterClassEx(nullptr);
		}

		WndClassEx wc = {};
		wc.cbSize = sizeof(wc);
		memcpy(&wc.style, lpWndClass, sizeof(*lpWndClass));
		wc.hIconSm = wc.hIcon;
		return origRegisterClassEx(&wc);
	}

	template <typename WndClassEx>
	ATOM registerClassEx(const WndClassEx* lpwcx, ATOM(WINAPI* origRegisterClassEx)(const WndClassEx*))
	{
		if (!lpwcx)
		{
			return origRegisterClassEx(nullptr);
		}

		WndClassEx wc = *lpwcx;
		if (!wc.hIconSm)
		{
			wc.hIconSm = wc.hIcon;
		}
		return origRegisterClassEx(&wc);
	}

	ATOM WINAPI registerClassA(const WNDCLASSA* lpWndClass)
	{
		LOG_FUNC("RegisterClassA", lpWndClass);
		return LOG_RESULT(registerClass(lpWndClass, CALL_ORIG_FUNC(RegisterClassExA)));
	}

	ATOM WINAPI registerClassW(const WNDCLASSW* lpWndClass)
	{
		LOG_FUNC("RegisterClassW", lpWndClass);
		return LOG_RESULT(registerClass(lpWndClass, CALL_ORIG_FUNC(RegisterClassExW)));
	}

	ATOM WINAPI registerClassExA(const WNDCLASSEXA* lpwcx)
	{
		LOG_FUNC("RegisterClassExA", lpwcx);
		return LOG_RESULT(registerClassEx(lpwcx, CALL_ORIG_FUNC(RegisterClassExA)));
	}

	ATOM WINAPI registerClassExW(const WNDCLASSEXW* lpwcx)
	{
		LOG_FUNC("RegisterClassExW", lpwcx);
		return LOG_RESULT(registerClassEx(lpwcx, CALL_ORIG_FUNC(RegisterClassExW)));
	}
}

#define HOOK_ICON_FUNCTION(module, func) hookIconFunc<decltype(&func), &func>(#module, #func)

namespace Gdi
{
	namespace Icon
	{
		void installHooks()
		{
			HOOK_ICON_FUNCTION(user32, CopyIcon);
			HOOK_ICON_FUNCTION(user32, CopyImage);
			HOOK_ICON_FUNCTION(user32, CreateCursor);
			HOOK_ICON_FUNCTION(user32, CreateIcon);
			HOOK_ICON_FUNCTION(user32, CreateIconFromResource);
			HOOK_ICON_FUNCTION(user32, CreateIconFromResourceEx);
			HOOK_ICON_FUNCTION(user32, CreateIconIndirect);
			HOOK_ICON_FUNCTION(user32, LoadCursorA);
			HOOK_ICON_FUNCTION(user32, LoadCursorW);
			HOOK_ICON_FUNCTION(user32, LoadCursorFromFileA);
			HOOK_ICON_FUNCTION(user32, LoadCursorFromFileW);
			HOOK_ICON_FUNCTION(user32, LoadIconA);
			HOOK_ICON_FUNCTION(user32, LoadIconW);
			HOOK_ICON_FUNCTION(user32, LoadImageA);
			HOOK_ICON_FUNCTION(user32, LoadImageW);
			HOOK_ICON_FUNCTION(user32, PrivateExtractIconsA);
			HOOK_ICON_FUNCTION(user32, PrivateExtractIconsW);

			HOOK_FUNCTION(user32, RegisterClassA, registerClassA);
			HOOK_FUNCTION(user32, RegisterClassW, registerClassW);
			HOOK_FUNCTION(user32, RegisterClassExA, registerClassExA);
			HOOK_FUNCTION(user32, RegisterClassExW, registerClassExW);
		}
	}
}
