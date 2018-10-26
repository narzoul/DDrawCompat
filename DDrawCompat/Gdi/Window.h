#pragma once

#define WIN32_LEAN_AND_MEAN

#include <map>
#include <memory>

#include <Windows.h>

#include "Gdi/Region.h"

namespace Gdi
{
	class Window
	{
	public:
		Window(HWND hwnd);
		Window(const Window&) = delete;
		Window& operator=(const Window&) = delete;

		HWND getPresentationWindow() const;
		Region getVisibleRegion() const;
		RECT getWindowRect() const;
		void updateWindow();

		static bool add(HWND hwnd);
		static std::shared_ptr<Window> get(HWND hwnd);
		static void remove(HWND hwnd);

		static std::map<HWND, std::shared_ptr<Window>> getWindows();
		static bool isPresentationWindow(HWND hwnd);
		static bool isTopLevelNonLayeredWindow(HWND hwnd);
		static void updateAll();

	private:
		void calcInvalidatedRegion(const RECT& oldWindowRect, const Region& oldVisibleRegion);
		void update();

		HWND m_hwnd;
		HWND m_presentationWindow;
		RECT m_windowRect;
		Region m_visibleRegion;
		Region m_invalidatedRegion;
		bool m_isUpdating;

		static std::map<HWND, std::shared_ptr<Window>> s_windows;
	};
}
