#include "Gdi/Gdi.h"
#include "Gdi/VirtualScreen.h"
#include "Gdi/Window.h"

extern "C" IMAGE_DOS_HEADER __ImageBase;

namespace
{
	ATOM registerPresentationWindowClass();

	ATOM getPresentationWindowClassAtom()
	{
		static ATOM atom = registerPresentationWindowClass();
		return atom;
	}

	LRESULT CALLBACK presentationWindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
	{
		return CALL_ORIG_FUNC(DefWindowProc)(hwnd, uMsg, wParam, lParam);
	}

	ATOM registerPresentationWindowClass()
	{
		WNDCLASS wc = {};
		wc.lpfnWndProc = &presentationWindowProc;
		wc.hInstance = reinterpret_cast<HINSTANCE>(&__ImageBase);
		wc.lpszClassName = "DDrawCompatPresentationWindow";
		return RegisterClass(&wc);
	}
}

namespace Gdi
{
	Window::Window(HWND hwnd)
		: m_hwnd(hwnd)
		, m_windowRect{ 0, 0, 0, 0 }
		, m_isUpdating(false)
	{
		m_presentationWindow = CreateWindowEx(
			WS_EX_LAYERED | WS_EX_TRANSPARENT,
			reinterpret_cast<const char*>(getPresentationWindowClassAtom()),
			nullptr,
			WS_DISABLED | WS_POPUP,
			0, 0, 1, 1,
			m_hwnd,
			nullptr,
			nullptr,
			nullptr);
		SetLayeredWindowAttributes(m_presentationWindow, 0, 255, LWA_ALPHA);
		update();
	}

	Window* Window::add(HWND hwnd)
	{
		auto it = s_windows.find(hwnd);
		if (it != s_windows.end())
		{
			return &it->second;
		}

		if (isPresentationWindow(hwnd))
		{
			return nullptr;
		}

		return &s_windows.emplace(hwnd, hwnd).first->second;
	}

	void Window::calcInvalidatedRegion(const RECT& oldWindowRect, const Region& oldVisibleRegion)
	{
		if (IsRectEmpty(&m_windowRect) || m_visibleRegion.isEmpty())
		{
			m_invalidatedRegion = Region();
			return;
		}

		m_invalidatedRegion = m_visibleRegion;

		if (m_windowRect.right - m_windowRect.left == oldWindowRect.right - oldWindowRect.left &&
			m_windowRect.bottom - m_windowRect.top == oldWindowRect.bottom - oldWindowRect.top)
		{
			Region preservedRegion(oldVisibleRegion);
			preservedRegion.offset(m_windowRect.left - oldWindowRect.left, m_windowRect.top - oldWindowRect.top);
			preservedRegion &= m_visibleRegion;

			if (!preservedRegion.isEmpty())
			{
				HDC screenDc = Gdi::getScreenDc();
				SelectClipRgn(screenDc, preservedRegion);
				BitBlt(screenDc, m_windowRect.left, m_windowRect.top,
					oldWindowRect.right - oldWindowRect.left, oldWindowRect.bottom - oldWindowRect.top,
					screenDc, oldWindowRect.left, oldWindowRect.top, SRCCOPY);
				SelectClipRgn(screenDc, nullptr);

				m_invalidatedRegion -= preservedRegion;
			}
		}
	}

	Window* Window::get(HWND hwnd)
	{
		auto it = s_windows.find(hwnd);
		return it != s_windows.end() ? &it->second : nullptr;
	}

	Region Window::getVisibleRegion() const
	{
		return m_visibleRegion;
	}

	RECT Window::getWindowRect() const
	{
		return m_windowRect;
	}

	bool Window::isPresentationWindow(HWND hwnd)
	{
		return GetClassLong(hwnd, GCW_ATOM) == getPresentationWindowClassAtom();
	}

	void Window::remove(HWND hwnd)
	{
		s_windows.erase(hwnd);
	}

	void Window::update()
	{
		if (m_isUpdating)
		{
			return;
		}
		m_isUpdating = true;

		RECT newWindowRect = {};
		Region newVisibleRegion;

		if (IsWindowVisible(m_hwnd) && !IsIconic(m_hwnd))
		{
			GetWindowRect(m_hwnd, &newWindowRect);
			if (!IsRectEmpty(&newWindowRect))
			{
				HDC windowDc = GetWindowDC(m_hwnd);
				GetRandomRgn(windowDc, newVisibleRegion, SYSRGN);
				ReleaseDC(m_hwnd, windowDc);
				newVisibleRegion &= VirtualScreen::getRegion();
			}

			SetWindowPos(m_presentationWindow, nullptr, newWindowRect.left, newWindowRect.top,
				newWindowRect.right - newWindowRect.left, newWindowRect.bottom - newWindowRect.top,
				SWP_SHOWWINDOW | SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_NOACTIVATE | SWP_NOSENDCHANGING | SWP_NOREDRAW);
		}

		std::swap(m_windowRect, newWindowRect);
		swap(m_visibleRegion, newVisibleRegion);

		calcInvalidatedRegion(newWindowRect, newVisibleRegion);

		m_isUpdating = false;
	}

	void Window::updateAll()
	{
		for (auto& windowPair : s_windows)
		{
			windowPair.second.update();
		}

		for (auto& windowPair : s_windows)
		{
			if (!windowPair.second.m_invalidatedRegion.isEmpty())
			{
				POINT clientOrigin = {};
				ClientToScreen(windowPair.first, &clientOrigin);
				windowPair.second.m_invalidatedRegion.offset(-clientOrigin.x, -clientOrigin.y);
				RedrawWindow(windowPair.first, nullptr, windowPair.second.m_invalidatedRegion,
					RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN | RDW_ERASENOW);
			}
		}
	}

	std::map<HWND, Window> Window::s_windows;
}
