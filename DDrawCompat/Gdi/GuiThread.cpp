#include <ShObjIdl.h>
#include <Windows.h>

#include <Common/Log.h>
#include <Common/Hook.h>
#include <Config/Settings/ConfigHotKey.h>
#include <Config/Settings/StatsHotKey.h>
#include <D3dDdi/ScopedCriticalSection.h>
#include <Dll/Dll.h>
#include <DDraw/RealPrimarySurface.h>
#include <Gdi/GuiThread.h>
#include <Gdi/Region.h>
#include <Gdi/WinProc.h>
#include <Overlay/ConfigWindow.h>
#include <Overlay/StatsWindow.h>
#include <Win32/DisplayMode.h>
#include <Win32/DpiAwareness.h>

namespace
{
	const UINT WM_USER_EXECUTE = WM_USER;

	struct EnumWindowsArgs
	{
		WNDENUMPROC lpEnumFunc;
		LPARAM lParam;
	};

	unsigned g_threadId = 0;
	Overlay::ConfigWindow* g_configWindow = nullptr;
	Overlay::StatsWindow* g_statsWindow = nullptr;
	HWND g_messageWindow = nullptr;
	bool g_isReady = false;

	BOOL CALLBACK enumWindowsProc(HWND hwnd, LPARAM lParam)
	{
		if (Gdi::GuiThread::isGuiThreadWindow(hwnd))
		{
			return TRUE;
		}
		auto& args = *reinterpret_cast<EnumWindowsArgs*>(lParam);
		return args.lpEnumFunc(hwnd, args.lParam);
	}

	BOOL WINAPI enumChildWindows(HWND hWndParent, WNDENUMPROC lpEnumFunc, LPARAM lParam)
	{
		LOG_FUNC("EnumWindows", hWndParent, lpEnumFunc, lParam);
		if (!lpEnumFunc)
		{
			return LOG_RESULT(CALL_ORIG_FUNC(EnumChildWindows)(hWndParent, lpEnumFunc, lParam));
		}
		EnumWindowsArgs args = { lpEnumFunc, lParam };
		return LOG_RESULT(CALL_ORIG_FUNC(EnumChildWindows)(hWndParent, enumWindowsProc, reinterpret_cast<LPARAM>(&args)));
	}

	BOOL WINAPI enumDesktopWindows(HDESK hDesktop, WNDENUMPROC lpfn, LPARAM lParam)
	{
		LOG_FUNC("EnumDesktopWindows", hDesktop, lpfn, lParam);
		if (!lpfn)
		{
			return LOG_RESULT(CALL_ORIG_FUNC(EnumDesktopWindows)(hDesktop, lpfn, lParam));
		}
		EnumWindowsArgs args = { lpfn, lParam };
		return LOG_RESULT(CALL_ORIG_FUNC(EnumDesktopWindows)(hDesktop, enumWindowsProc, reinterpret_cast<LPARAM>(&args)));
	}

	BOOL WINAPI enumThreadWindows(DWORD dwThreadId, WNDENUMPROC lpfn, LPARAM lParam)
	{
		LOG_FUNC("EnumThreadWindows", dwThreadId, lpfn, lParam);
		if (lpfn && dwThreadId == g_threadId)
		{
			return LOG_RESULT(FALSE);
		}
		return LOG_RESULT(CALL_ORIG_FUNC(EnumThreadWindows)(dwThreadId, lpfn, lParam));
	}

	BOOL WINAPI enumWindows(WNDENUMPROC lpEnumFunc, LPARAM lParam)
	{
		LOG_FUNC("EnumWindows", lpEnumFunc, lParam);
		if (!lpEnumFunc)
		{
			return LOG_RESULT(CALL_ORIG_FUNC(EnumWindows)(lpEnumFunc, lParam));
		}
		EnumWindowsArgs args = { lpEnumFunc, lParam };
		return LOG_RESULT(CALL_ORIG_FUNC(EnumWindows)(enumWindowsProc, reinterpret_cast<LPARAM>(&args)));
	}

	template <typename Char, typename FindWindowExProc>
	HWND WINAPI findWindowEx(HWND hWndParent, HWND hWndChildAfter, const Char* lpszClass, const Char* lpszWindow,
		FindWindowExProc origFindWindowEx)
	{
		HWND hwnd = origFindWindowEx(hWndParent, hWndChildAfter, lpszClass, lpszWindow);
		while (hwnd && Gdi::GuiThread::isGuiThreadWindow(hwnd))
		{
			hwnd = origFindWindowEx(hWndParent, hwnd, lpszClass, lpszWindow);
		}
		return hwnd;
	}

	HWND WINAPI findWindowA(LPCSTR lpClassName, LPCSTR lpWindowName)
	{
		LOG_FUNC("FindWindowA", lpClassName, lpWindowName);
		return LOG_RESULT(findWindowEx(nullptr, nullptr, lpClassName, lpWindowName, CALL_ORIG_FUNC(FindWindowExA)));
	}

	HWND WINAPI findWindowW(LPCWSTR lpClassName, LPCWSTR lpWindowName)
	{
		LOG_FUNC("FindWindowW", lpClassName, lpWindowName);
		return LOG_RESULT(findWindowEx(nullptr, nullptr, lpClassName, lpWindowName, CALL_ORIG_FUNC(FindWindowExW)));
	}

	HWND WINAPI findWindowExA(HWND hWndParent, HWND hWndChildAfter, LPCSTR lpszClass, LPCSTR lpszWindow)
	{
		LOG_FUNC("FindWindowExA", hWndParent, hWndChildAfter, lpszClass, lpszWindow);
		return LOG_RESULT(findWindowEx(hWndParent, hWndChildAfter, lpszClass, lpszWindow, CALL_ORIG_FUNC(FindWindowExA)));
	}

	HWND WINAPI findWindowExW(HWND hWndParent, HWND hWndChildAfter, LPCWSTR lpszClass, LPCWSTR lpszWindow)
	{
		LOG_FUNC("FindWindowExW", hWndParent, hWndChildAfter, lpszClass, lpszWindow);
		return LOG_RESULT(findWindowEx(hWndParent, hWndChildAfter, lpszClass, lpszWindow, CALL_ORIG_FUNC(FindWindowExW)));
	}

	HWND getNonGuiThreadWindow(HWND hWnd, UINT uCmd)
	{
		while (hWnd && Gdi::GuiThread::isGuiThreadWindow(hWnd))
		{
			hWnd = CALL_ORIG_FUNC(GetWindow)(hWnd, uCmd);
		}
		return hWnd;
	}

	HWND WINAPI getTopWindow(HWND hWnd)
	{
		LOG_FUNC("GetTopWindow", hWnd);
		return LOG_RESULT(getNonGuiThreadWindow(CALL_ORIG_FUNC(GetTopWindow)(hWnd), GW_HWNDNEXT));
	}

	HWND WINAPI getWindow(HWND hWnd, UINT uCmd)
	{
		LOG_FUNC("GetWindow", hWnd, uCmd);
		HWND result = CALL_ORIG_FUNC(GetWindow)(hWnd, uCmd);
		switch (uCmd)
		{
		case GW_CHILD:
		case GW_HWNDFIRST:
		case GW_HWNDNEXT:
			return LOG_RESULT(getNonGuiThreadWindow(result, GW_HWNDNEXT));

		case GW_HWNDLAST:
		case GW_HWNDPREV:
			return LOG_RESULT(getNonGuiThreadWindow(result, GW_HWNDPREV));
		}
		return LOG_RESULT(result);
	}

	BOOL CALLBACK initChildWindow(HWND hwnd, LPARAM /*lParam*/)
	{
		Gdi::WinProc::onCreateWindow(hwnd);
		return TRUE;
	}

	BOOL CALLBACK initTopLevelWindow(HWND hwnd, LPARAM /*lParam*/)
	{
		DWORD windowPid = 0;
		GetWindowThreadProcessId(hwnd, &windowPid);
		if (GetCurrentProcessId() == windowPid)
		{
			Gdi::WinProc::onCreateWindow(hwnd);
			CALL_ORIG_FUNC(EnumChildWindows)(hwnd, &initChildWindow, 0);
			if (8 == Win32::DisplayMode::getBpp())
			{
				PostMessage(hwnd, WM_PALETTECHANGED, reinterpret_cast<WPARAM>(GetDesktopWindow()), 0);
			}
		}
		return TRUE;
	}

	LRESULT CALLBACK messageWindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
	{
		LOG_FUNC("messageWindowProc", Compat::WindowMessageStruct(hwnd, uMsg, wParam, lParam));

		if (WM_USER_EXECUTE == uMsg)
		{
			auto& func = *reinterpret_cast<const std::function<void()>*>(lParam);
			func();
			return LOG_RESULT(0);
		}

		return LOG_RESULT(CALL_ORIG_FUNC(DefWindowProc)(hwnd, uMsg, wParam, lParam));
	}

	unsigned WINAPI messageWindowThreadProc(LPVOID /*lpParameter*/)
	{
		ImmDisableIME(0);
		CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

		WNDCLASS wc = {};
		wc.lpfnWndProc = &messageWindowProc;
		wc.hInstance = Dll::g_currentModule;
		wc.lpszClassName = "DDrawCompatMessageWindow";
		CALL_ORIG_FUNC(RegisterClassA)(&wc);

		g_messageWindow = CreateWindow(
			"DDrawCompatMessageWindow", nullptr, 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, nullptr, nullptr);
		if (!g_messageWindow)
		{
			LOG_INFO << "ERROR: Failed to create a message-only window";
			return 0;
		}

		if (0 != Config::statsHotKey.get().vk)
		{
			static Overlay::StatsWindow statsWindow;
			g_statsWindow = &statsWindow;
		}

		if (0 != Config::configHotKey.get().vk)
		{
			static Overlay::ConfigWindow configWindow;
			g_configWindow = &configWindow;
		}

		{
			D3dDdi::ScopedCriticalSection lock;
			g_isReady = true;
			CALL_ORIG_FUNC(EnumWindows)(initTopLevelWindow, 0);
		}

		MSG msg = {};
		while (CALL_ORIG_FUNC(GetMessageA)(&msg, nullptr, 0, 0))
		{
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}

		return 0;
	}
}

namespace Gdi
{
	namespace GuiThread
	{
		HWND createWindow(DWORD dwExStyle, LPCWSTR lpClassName, LPCWSTR lpWindowName, DWORD dwStyle, int X, int Y,
			int nWidth, int nHeight, HWND hWndParent, HMENU hMenu, HINSTANCE hInstance, LPVOID lpParam, bool dpiAware)
		{
			// Workaround for ForceSimpleWindow shim
			static auto createWindowExW = reinterpret_cast<decltype(&CreateWindowExW)>(
				Compat::getProcAddress(GetModuleHandle("user32"), "CreateWindowExW"));

			HWND hwnd = nullptr;
			execute([&]()
				{
					Win32::ScopedDpiAwareness dpiAwareness(dpiAware);
					hwnd = createWindowExW(dwExStyle, lpClassName, lpWindowName, dwStyle, X, Y, nWidth, nHeight,
						hWndParent, hMenu, hInstance, lpParam);
				});
			return hwnd;
		}

		void deleteTaskbarTab(HWND hwnd)
		{
			execute([&]()
				{
					ITaskbarList* taskbarList = nullptr;
					if (SUCCEEDED(CoCreateInstance(CLSID_TaskbarList, nullptr, CLSCTX_INPROC_SERVER, IID_ITaskbarList,
						reinterpret_cast<void**>(&taskbarList))))
					{
						taskbarList->lpVtbl->HrInit(taskbarList);
						taskbarList->lpVtbl->DeleteTab(taskbarList, hwnd);
						taskbarList->lpVtbl->Release(taskbarList);
					}
				});
		}

		void destroyWindow(HWND hwnd)
		{
			PostMessage(hwnd, WM_CLOSE, 0, 0);
		}

		void executeFunc(const std::function<void()>& func)
		{
			DWORD_PTR result = 0;
			SendMessageTimeout(g_messageWindow, WM_USER_EXECUTE, 0, reinterpret_cast<LPARAM>(&func),
				SMTO_BLOCK | SMTO_NOTIMEOUTIFNOTHUNG, 0, &result);
		}

		Overlay::ConfigWindow* getConfigWindow()
		{
			return g_configWindow;
		}

		Overlay::StatsWindow* getStatsWindow()
		{
			return g_statsWindow;
		}

		void installHooks()
		{
			Dll::createThread(messageWindowThreadProc, &g_threadId, THREAD_PRIORITY_TIME_CRITICAL, 0);

			HOOK_FUNCTION(user32, EnumChildWindows, enumChildWindows);
			HOOK_FUNCTION(user32, EnumDesktopWindows, enumDesktopWindows);
			HOOK_FUNCTION(user32, EnumThreadWindows, enumThreadWindows);
			HOOK_FUNCTION(user32, EnumWindows, enumWindows);
			HOOK_FUNCTION(user32, FindWindowA, findWindowA);
			HOOK_FUNCTION(user32, FindWindowW, findWindowW);
			HOOK_FUNCTION(user32, FindWindowExA, findWindowExA);
			HOOK_FUNCTION(user32, FindWindowExW, findWindowExW);
			HOOK_FUNCTION(user32, GetTopWindow, getTopWindow);
			HOOK_FUNCTION(user32, GetWindow, getWindow);
		}

		bool isGuiThreadWindow(HWND hwnd)
		{
			return GetWindowThreadProcessId(hwnd, nullptr) == g_threadId;
		}

		bool isReady()
		{
			return g_isReady;
		}

		void setWindowRgn(HWND hwnd, Gdi::Region rgn)
		{
			execute([&]()
				{
					if (SetWindowRgn(hwnd, rgn, FALSE))
					{
						rgn.release();
					}
				});
		}
	}
}
