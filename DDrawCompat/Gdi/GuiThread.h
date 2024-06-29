#pragma once

#include <functional>

#include <Windows.h>

#include <Gdi/Region.h>

namespace Overlay
{
	class ConfigWindow;
	class StatsWindow;
}

namespace Gdi
{
	namespace GuiThread
	{
		HWND createWindow(DWORD dwExStyle, LPCWSTR lpClassName, LPCWSTR lpWindowName, DWORD dwStyle, int X, int Y,
			int nWidth, int nHeight, HWND hWndParent, HMENU hMenu, HINSTANCE hInstance, LPVOID lpParam, bool dpiAware);
		void deleteTaskbarTab(HWND hwnd);
		void destroyWindow(HWND hwnd);
		void setWindowRgn(HWND hwnd, Gdi::Region rgn);

		Overlay::ConfigWindow* getConfigWindow();
		Overlay::StatsWindow* getStatsWindow();

		template <typename Func>
		void execute(const Func& func) { executeFunc(std::cref(func)); }
		void executeAsyncFunc(void(*func)());
		void executeFunc(const std::function<void()>& func);

		bool isGuiThreadWindow(HWND hwnd);
		bool isReady();

		void installHooks();
	}
}
