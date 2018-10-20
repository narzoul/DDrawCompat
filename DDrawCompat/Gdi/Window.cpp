#include "DDraw/ScopedThreadLock.h"
#include "Gdi/Gdi.h"
#include "Gdi/VirtualScreen.h"
#include "Gdi/Window.h"

extern "C" IMAGE_DOS_HEADER __ImageBase;

namespace
{
	ATOM registerPresentationWindowClass();

	ATOM getComboLBoxAtom()
	{
		WNDCLASS wc = {};
		static ATOM comboLBoxAtom = static_cast<ATOM>(GetClassInfo(nullptr, "ComboLBox", &wc));
		return comboLBoxAtom;
	}

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
		, m_presentationWindow(nullptr)
		, m_windowRect{ 0, 0, 0, 0 }
		, m_isUpdating(false)
	{
		const ATOM atom = static_cast<ATOM>(GetClassLong(hwnd, GCW_ATOM));
		if (MENU_ATOM != atom && getComboLBoxAtom() != atom)
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
		}

		update();
	}

	bool Window::add(HWND hwnd)
	{
		const bool isTopLevelNonLayeredWindow = !(GetWindowLongPtr(hwnd, GWL_EXSTYLE) & WS_EX_LAYERED) &&
			(!(GetWindowLongPtr(hwnd, GWL_STYLE) & WS_CHILD) || GetParent(hwnd) == GetDesktopWindow() ||
				getComboLBoxAtom() == GetClassLong(hwnd, GCW_ATOM));

		if (isTopLevelNonLayeredWindow && !get(hwnd) &&
			GetClassLong(hwnd, GCW_ATOM) != getPresentationWindowClassAtom())
		{
			s_windows.emplace(hwnd, std::make_shared<Window>(hwnd));
			return true;
		}

		return false;
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

	std::shared_ptr<Window> Window::get(HWND hwnd)
	{
		DDraw::ScopedThreadLock lock;
		auto it = s_windows.find(hwnd);
		return it != s_windows.end() ? it->second : nullptr;
	}

	HWND Window::getPresentationWindow() const
	{
		return m_presentationWindow ? m_presentationWindow : m_hwnd;
	}

	Region Window::getVisibleRegion() const
	{
		DDraw::ScopedThreadLock lock;
		return m_visibleRegion;
	}

	RECT Window::getWindowRect() const
	{
		DDraw::ScopedThreadLock lock;
		return m_windowRect;
	}

	std::map<HWND, std::shared_ptr<Window>> Window::getWindows()
	{
		DDraw::ScopedThreadLock lock;
		return s_windows;
	}

	bool Window::isPresentationWindow(HWND hwnd)
	{
		return GetClassLong(hwnd, GCW_ATOM) == getPresentationWindowClassAtom();
	}

	void Window::remove(HWND hwnd)
	{
		DDraw::ScopedThreadLock lock;
		s_windows.erase(hwnd);
	}

	void Window::update()
	{
		DDraw::ScopedThreadLock lock;
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

			if (m_presentationWindow)
			{
				SetWindowPos(m_presentationWindow, nullptr, newWindowRect.left, newWindowRect.top,
					newWindowRect.right - newWindowRect.left, newWindowRect.bottom - newWindowRect.top,
					SWP_SHOWWINDOW | SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_NOACTIVATE | SWP_NOSENDCHANGING | SWP_NOREDRAW);
			}
		}
		else if (m_presentationWindow)
		{
			ShowWindow(m_presentationWindow, SW_HIDE);
		}

		std::swap(m_windowRect, newWindowRect);
		swap(m_visibleRegion, newVisibleRegion);

		calcInvalidatedRegion(newWindowRect, newVisibleRegion);

		m_isUpdating = false;
	}

	void Window::updateAll()
	{
		auto windows(getWindows());
		for (auto& windowPair : windows)
		{
			windowPair.second->update();
		}

		for (auto& windowPair : windows)
		{
			if (!windowPair.second->m_invalidatedRegion.isEmpty())
			{
				POINT clientOrigin = {};
				ClientToScreen(windowPair.first, &clientOrigin);
				windowPair.second->m_invalidatedRegion.offset(-clientOrigin.x, -clientOrigin.y);
				RedrawWindow(windowPair.first, nullptr, windowPair.second->m_invalidatedRegion,
					RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN | RDW_ERASENOW);
			}
		}
	}

	void Window::updateWindow()
	{
		RECT windowRect = {};
		GetWindowRect(m_hwnd, &windowRect);
		if (!EqualRect(&windowRect, &m_windowRect))
		{
			updateAll();
		}
	}

	std::map<HWND, std::shared_ptr<Window>> Window::s_windows;
}
