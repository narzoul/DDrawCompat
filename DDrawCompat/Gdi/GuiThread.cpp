#include <Windows.h>
#include <dwmapi.h>
#include <ShObjIdl.h>
#include <wincodec.h>

#include <Common/CompatPtr.h>
#include <Common/Log.h>
#include <Common/Hook.h>
#include <Common/Rect.h>
#include <Config/Settings/ConfigHotKey.h>
#include <Config/Settings/StatsHotKey.h>
#include <D3dDdi/Adapter.h>
#include <D3dDdi/ScopedCriticalSection.h>
#include <Dll/Dll.h>
#include <DDraw/RealPrimarySurface.h>
#include <Gdi/GuiThread.h>
#include <Gdi/Region.h>
#include <Gdi/WinProc.h>
#include <Input/Input.h>
#include <Overlay/ConfigWindow.h>
#include <Overlay/StatsWindow.h>
#include <Overlay/Steam.h>
#include <Win32/DisplayMode.h>
#include <Win32/DpiAwareness.h>

namespace
{
	const UINT WM_USER_EXECUTE = WM_USER;
	const UINT WM_USER_EXECUTE_ASYNC = WM_USER + 1;

	struct EnumWindowsArgs
	{
		WNDENUMPROC lpEnumFunc;
		LPARAM lParam;
	};

	unsigned g_threadId = 0;
	Overlay::ConfigWindow* g_configWindow = nullptr;
	Overlay::StatsWindow* g_statsWindow = nullptr;
	HWND g_messageWindow = nullptr;
	IWICImagingFactory* g_wicImagingFactory = nullptr;
	bool g_isReady = false;

	unsigned WINAPI screenshotThreadProc(LPVOID /*lpParameter*/);

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
		LOG_FUNC("EnumChildWindows", hWndParent, lpEnumFunc, lParam);
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

	RECT getScreenshotWindowRect()
	{
		LOG_FUNC("getScreenshotWindowRect");
		HWND foregroundWindow = GetForegroundWindow();
		LOG_DEBUG << "Foreground window: " << foregroundWindow;

		if (!foregroundWindow)
		{
			LOG_DEBUG << "Using desktop rect";
			RECT r = {};
			for (auto& mi : Win32::DisplayMode::getAllMonitorInfo())
			{
				UnionRect(&r, &r, &mi.second.rcReal);
			}
			return r;
		}

		DWORD pid = 0;
		GetWindowThreadProcessId(foregroundWindow, &pid);
		if (GetCurrentProcessId() != pid)
		{
			LOG_DEBUG << "Foreground window doesn't belong to current process";
			RECT r = {};
			DwmGetWindowAttribute(foregroundWindow, DWMWA_EXTENDED_FRAME_BOUNDS, &r, sizeof(r));
			return LOG_RESULT(r);
		}

		RECT windowRect = {};
		CALL_ORIG_FUNC(GetWindowRect)(foregroundWindow, &windowRect);
		LOG_DEBUG << "Foreground window rect: " << windowRect;

		auto dm = Win32::DisplayMode::getEmulatedDisplayMode();
		if (dm.deviceName.empty())
		{
			LOG_DEBUG << "No scaling needed";
			return LOG_RESULT(windowRect);
		}

		auto mi = Win32::DisplayMode::getMonitorInfo(dm.deviceName);
		if (!mi.isEmulated)
		{
			LOG_DEBUG << "Scaling lost";
			return LOG_RESULT(windowRect);
		}

		D3dDdi::ScopedCriticalSection lock;
		auto adapter = D3dDdi::Adapter::find(dm.deviceName);
		if (!adapter)
		{
			LOG_DEBUG << "Adapter not found";
			return LOG_RESULT(windowRect);
		}

		RECT screenRect = adapter->applyDisplayAspectRatio(mi.rcReal, Rect::getSize(mi.rcEmulated));
		Rect::transform(windowRect, mi.rcEmulated, screenRect);
		LOG_DEBUG << "Scaled window rect: " << windowRect;

		IntersectRect(&windowRect, &windowRect, &screenRect);
		return LOG_RESULT(windowRect);
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

	HBITMAP loadImage(const std::filesystem::path& path)
	{
		if (!g_wicImagingFactory)
		{
			return nullptr;
		}

		CompatPtr<IWICBitmapDecoder> decoder;
		g_wicImagingFactory->lpVtbl->CreateDecoderFromFilename(g_wicImagingFactory, path.c_str(), nullptr,
			GENERIC_READ, WICDecodeMetadataCacheOnDemand, &decoder.getRef());
		if (!decoder)
		{
			return nullptr;
		}

		CompatPtr<IWICBitmapFrameDecode> frameDecode;
		decoder.get()->lpVtbl->GetFrame(decoder, 0, &frameDecode.getRef());
		if (!frameDecode)
		{
			return nullptr;
		}

		UINT width = 0;
		UINT height = 0;
		WICPixelFormatGUID pixelFormat = {};
		if (FAILED(frameDecode.get()->lpVtbl->GetSize(frameDecode, &width, &height)) ||
			FAILED(frameDecode.get()->lpVtbl->GetPixelFormat(frameDecode, &pixelFormat)) ||
			0 == width || 0 == height)
		{
			return nullptr;
		}

		CompatPtr<IWICBitmapSource> bitmapSource;
		if (pixelFormat == GUID_WICPixelFormat32bppBGRA)
		{
			bitmapSource.reset(reinterpret_cast<IWICBitmapSource*>(frameDecode.get()));
			bitmapSource.get()->lpVtbl->AddRef(bitmapSource);
		}
		else
		{
			WICConvertBitmapSource(GUID_WICPixelFormat32bppBGRA,
				reinterpret_cast<IWICBitmapSource*>(frameDecode.get()), &bitmapSource.getRef());
			if (!bitmapSource)
			{
				return nullptr;
			}
		}

		std::vector<BYTE> imageBits(4 * width * height);
		if (FAILED(bitmapSource.get()->lpVtbl->CopyPixels(
			bitmapSource, nullptr, 4 * width, imageBits.size(), imageBits.data())))
		{
			return nullptr;
		}

		return CALL_ORIG_FUNC(CreateBitmap)(width, height, 1, 32, imageBits.data());
	}

	LRESULT CALLBACK messageWindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
	{
		if (WM_USER_EXECUTE == uMsg)
		{
			auto& func = *reinterpret_cast<const std::function<void()>*>(lParam);
			func();
			return 0;
		}

		if (WM_USER_EXECUTE_ASYNC == uMsg)
		{
			auto func = reinterpret_cast<void(*)()>(lParam);
			func();
			return 0;
		}

		if (WM_HOTKEY == uMsg)
		{
			static HANDLE thread = nullptr;
			if (thread && WAIT_OBJECT_0 == WaitForSingleObject(thread, 0))
			{
				CloseHandle(thread);
				thread = nullptr;
			}

			if (thread)
			{
				LOG_DEBUG << "Previous screenshot thread is still running";
			}
			else
			{
				thread = Dll::createThread(&screenshotThreadProc, nullptr, THREAD_PRIORITY_TIME_CRITICAL);
			}
		}

		return CALL_ORIG_FUNC(DefWindowProc)(hwnd, uMsg, wParam, lParam);
	}

	unsigned WINAPI messageWindowThreadProc(LPVOID /*lpParameter*/)
	{
		ImmDisableIME(0);
		CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

		ITaskbarList* taskbarList = nullptr;
		if (SUCCEEDED(CoCreateInstance(CLSID_TaskbarList, nullptr, CLSCTX_INPROC_SERVER, IID_ITaskbarList,
			reinterpret_cast<void**>(&taskbarList))))
		{
			taskbarList->lpVtbl->Release(taskbarList);
			taskbarList = nullptr;
		}

		CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_IWICImagingFactory,
			reinterpret_cast<void**>(&g_wicImagingFactory));

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
			D3dDdi::MetaShader::loadBitmaps();
			g_isReady = true;
			CALL_ORIG_FUNC(EnumWindows)(initTopLevelWindow, 0);
		}

		Input::init();
		RegisterHotKey(g_messageWindow, 0, MOD_ALT, VK_SNAPSHOT);
		RegisterHotKey(g_messageWindow, 1, MOD_ALT | MOD_CONTROL, VK_SNAPSHOT);
		RegisterHotKey(g_messageWindow, 2, MOD_ALT | MOD_CONTROL | MOD_SHIFT, VK_SNAPSHOT);

		MSG msg = {};
		while (CALL_ORIG_FUNC(GetMessageA)(&msg, nullptr, 0, 0))
		{
			DispatchMessage(&msg);
		}

		return 0;
	}

	unsigned WINAPI screenshotThreadProc(LPVOID /*lpParameter*/)
	{
		LOG_FUNC("screenshotThreadProc");
		RECT rect = getScreenshotWindowRect();
		if (IsRectEmpty(&rect))
		{
			return LOG_RESULT(1);
		}

		std::unique_ptr<HDC__, void(*)(HDC)> srcDc(GetDC(nullptr), [](HDC dc) { ReleaseDC(nullptr, dc); });
		std::unique_ptr<HDC__, decltype(&DeleteDC)> dstDc(CreateCompatibleDC(nullptr), DeleteDC);
		std::unique_ptr<HBITMAP__, decltype(&DeleteObject)> bmp(
			CALL_ORIG_FUNC(CreateCompatibleBitmap)(srcDc.get(), rect.right - rect.left, rect.bottom - rect.top), DeleteObject);
		if (!srcDc || !dstDc || !bmp)
		{
			return LOG_RESULT(2);
		}

		auto oldBmp = SelectObject(dstDc.get(), bmp.get());
		CALL_ORIG_FUNC(BitBlt)(dstDc.get(), 0, 0, rect.right - rect.left, rect.bottom - rect.top,
			srcDc.get(), rect.left, rect.top, SRCCOPY | CAPTUREBLT);
		SelectObject(dstDc.get(), oldBmp);

		if (!OpenClipboard(GetDesktopWindow()))
		{
			return LOG_RESULT(3);
		}

		class ClipBoardGuard
		{
		public:
			~ClipBoardGuard() { CloseClipboard(); }
		} clipBoardGuard;

		if (!EmptyClipboard())
		{
			return LOG_RESULT(4);
		}

		if (!SetClipboardData(CF_BITMAP, bmp.get()))
		{
			return LOG_RESULT(5);
		}

		bmp.release();
		return LOG_RESULT(0);
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
					Win32::ScopedDpiAwareness dpiAwareness(dpiAware ? DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 : nullptr);
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
			Overlay::Steam::onDestroyWindow(hwnd);
			PostMessage(hwnd, WM_CLOSE, 0, 0);
		}

		void executeAsyncFunc(void(*func)())
		{
			PostMessage(g_messageWindow, WM_USER_EXECUTE_ASYNC, 0, reinterpret_cast<LPARAM>(func));
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
					if (CALL_ORIG_FUNC(SetWindowRgn)(hwnd, rgn, FALSE))
					{
						rgn.release();
					}
				});
		}

		HBITMAP wicLoadImage(const std::filesystem::path& path)
		{
			HBITMAP bitmap = nullptr;
			execute([&]()
				{
					bitmap = loadImage(path);
				});
			return bitmap;
		}
	}
}
