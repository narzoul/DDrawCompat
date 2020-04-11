#pragma once

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
		~Window();

		BYTE getAlpha() const;
		COLORREF getColorKey() const;
		HWND getPresentationWindow() const;
		Region getVisibleRegion() const;
		RECT getWindowRect() const;
		bool isLayered() const;
		void setPresentationWindow(HWND hwnd);
		void updateWindow();

		static bool add(HWND hwnd);
		static std::shared_ptr<Window> get(HWND hwnd);
		static void remove(HWND hwnd);

		static std::map<HWND, std::shared_ptr<Window>> getWindows();
		static bool isPresentationWindow(HWND hwnd);
		static bool isTopLevelWindow(HWND hwnd);
		static void updateAll();
		static void updateLayeredWindowInfo(HWND hwnd, COLORREF colorKey, BYTE alpha);

		static void installHooks();
		static void uninstallHooks();

	private:
		void calcInvalidatedRegion(const RECT& oldWindowRect, const Region& oldVisibleRegion);
		void update();

		HWND m_hwnd;
		HWND m_presentationWindow;
		RECT m_windowRect;
		Region m_visibleRegion;
		Region m_invalidatedRegion;
		COLORREF m_colorKey;
		BYTE m_alpha;
		bool m_isLayered;

		static std::map<HWND, std::shared_ptr<Window>> s_windows;
	};
}
