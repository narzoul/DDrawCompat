#include "Gdi/Gdi.h"
#include "Gdi/Window.h"

namespace Gdi
{
	Window::Window(HWND hwnd)
		: m_hwnd(hwnd)
		, m_windowRect{ 0, 0, 0, 0 }
		, m_isUpdating(false)
	{
		update();
	}

	Window& Window::add(HWND hwnd)
	{
		auto it = s_windows.find(hwnd);
		if (it != s_windows.end())
		{
			return it->second;
		}

		return s_windows.emplace(hwnd, hwnd).first->second;
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
				HDC screenDc = GetDC(nullptr);
				SelectClipRgn(screenDc, preservedRegion);
				BitBlt(screenDc, m_windowRect.left, m_windowRect.top,
					oldWindowRect.right - oldWindowRect.left, oldWindowRect.bottom - oldWindowRect.top,
					screenDc, oldWindowRect.left, oldWindowRect.top, SRCCOPY);
				ReleaseDC(nullptr, screenDc);

				m_invalidatedRegion -= preservedRegion;
			}
		}
	}

	Window& Window::get(HWND hwnd)
	{
		return add(hwnd);
	}

	Region Window::getVisibleRegion() const
	{
		return m_visibleRegion;
	}

	RECT Window::getWindowRect() const
	{
		return m_windowRect;
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
				newVisibleRegion &= getVirtualScreenRegion();
			}
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
