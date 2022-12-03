#include <ShObjIdl.h>
#include <Windows.h>

#include <Common/Log.h>
#include <Common/Hook.h>
#include <D3dDdi/ScopedCriticalSection.h>
#include <Dll/Dll.h>
#include <DDraw/RealPrimarySurface.h>
#include <Gdi/GuiThread.h>
#include <Gdi/Region.h>
#include <Gdi/WinProc.h>
#include <Overlay/ConfigWindow.h>
#include <Overlay/StatsWindow.h>
#include <Win32/DisplayMode.h>

namespace
{
	const UINT WM_USER_EXECUTE = WM_USER;

	unsigned g_threadId = 0;
	Overlay::ConfigWindow* g_configWindow = nullptr;
	Overlay::StatsWindow* g_statsWindow = nullptr;
	HWND g_messageWindow = nullptr;
	bool g_isReady = false;

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
			EnumChildWindows(hwnd, &initChildWindow, 0);
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

		Overlay::StatsWindow statsWindow;
		g_statsWindow = &statsWindow;

		Overlay::ConfigWindow configWindow;
		g_configWindow = &configWindow;

		{
			D3dDdi::ScopedCriticalSection lock;
			g_isReady = true;
			EnumWindows(initTopLevelWindow, 0);
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
		HWND createWindow(DWORD dwExStyle, LPCSTR lpClassName, LPCSTR lpWindowName, DWORD dwStyle,
			int X, int Y, int nWidth, int nHeight, HWND hWndParent, HMENU hMenu, HINSTANCE hInstance, LPVOID lpParam)
		{
			// Workaround for ForceSimpleWindow shim
			static auto createWindowExA = reinterpret_cast<decltype(&CreateWindowExA)>(
				Compat::getProcAddress(GetModuleHandle("user32"), "CreateWindowExA"));

			HWND hwnd = nullptr;
			execute([&]()
				{
					hwnd = createWindowExA(dwExStyle, lpClassName, lpWindowName, dwStyle, X, Y, nWidth, nHeight,
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
			execute([&]() { DestroyWindow(hwnd); });
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

		void start()
		{
			Dll::createThread(messageWindowThreadProc, &g_threadId, THREAD_PRIORITY_TIME_CRITICAL, 0);
		}
	}
}
