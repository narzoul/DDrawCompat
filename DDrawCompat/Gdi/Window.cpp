#include "Common/Hook.h"
#include "Common/Log.h"
#include "DDraw/ScopedThreadLock.h"
#include "Gdi/Gdi.h"
#include "Gdi/Window.h"

extern "C" IMAGE_DOS_HEADER __ImageBase;

namespace
{
	const UINT WM_CREATEPRESENTATIONWINDOW = WM_USER;

	HANDLE g_presentationWindowThread = nullptr;
	DWORD g_presentationWindowThreadId = 0;
	HWND g_messageWindow = nullptr;
	
	ATOM getComboLBoxAtom()
	{
		WNDCLASS wc = {};
		static ATOM comboLBoxAtom = static_cast<ATOM>(GetClassInfo(nullptr, "ComboLBox", &wc));
		return comboLBoxAtom;
	}

	LRESULT CALLBACK messageWindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
	{
		LOG_FUNC("messageWindowProc", hwnd, Compat::hex(uMsg), Compat::hex(wParam), Compat::hex(lParam));
		switch (uMsg)
		{
		case WM_CREATEPRESENTATIONWINDOW:
		{
			HWND presentationWindow = CreateWindowEx(
				WS_EX_LAYERED | WS_EX_TRANSPARENT,
				"DDrawCompatPresentationWindow",
				nullptr,
				WS_DISABLED | WS_POPUP,
				0, 0, 1, 1,
				reinterpret_cast<HWND>(wParam),
				nullptr,
				nullptr,
				nullptr);
			SetLayeredWindowAttributes(presentationWindow, 0, 255, LWA_ALPHA);
			return reinterpret_cast<LRESULT>(presentationWindow);
		}

		case WM_DESTROY:
			PostQuitMessage(0);
			return 0;

		default:
			return CALL_ORIG_FUNC(DefWindowProc)(hwnd, uMsg, wParam, lParam);
		}
	}

	LRESULT CALLBACK presentationWindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
	{
		LOG_FUNC("presentationWindowProc", hwnd, Compat::hex(uMsg), Compat::hex(wParam), Compat::hex(lParam));
		return CALL_ORIG_FUNC(DefWindowProc)(hwnd, uMsg, wParam, lParam);
	}

	DWORD WINAPI presentationWindowThreadProc(LPVOID /*lpParameter*/)
	{
		WNDCLASS wc = {};
		wc.lpfnWndProc = &messageWindowProc;
		wc.hInstance = reinterpret_cast<HINSTANCE>(&__ImageBase);
		wc.lpszClassName = "DDrawCompatMessageWindow";
		RegisterClass(&wc);

		g_messageWindow = CreateWindow(
			"DDrawCompatMessageWindow", nullptr, 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, nullptr, nullptr);
		if (!g_messageWindow)
		{
			return 0;
		}

		MSG msg = {};
		while (GetMessage(&msg, nullptr, 0, 0))
		{
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}

		return 0;
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
			m_presentationWindow = reinterpret_cast<HWND>(SendMessage(
				g_messageWindow, WM_CREATEPRESENTATIONWINDOW, reinterpret_cast<WPARAM>(hwnd), 0));
		}

		update();
	}

	bool Window::add(HWND hwnd)
	{
		if (isTopLevelNonLayeredWindow(hwnd) && !get(hwnd))
		{
			DDraw::ScopedThreadLock lock;
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

	void Window::installHooks()
	{
		WNDCLASS wc = {};
		wc.lpfnWndProc = &presentationWindowProc;
		wc.hInstance = reinterpret_cast<HINSTANCE>(&__ImageBase);
		wc.lpszClassName = "DDrawCompatPresentationWindow";
		RegisterClass(&wc);

		g_presentationWindowThread = CreateThread(
			nullptr, 0, &presentationWindowThreadProc, nullptr, 0, &g_presentationWindowThreadId);

		int i = 0;
		while (!g_messageWindow && i < 1000)
		{
			Sleep(1);
			++i;
		}

		if (!g_messageWindow)
		{
			Compat::Log() << "Failed to create a message-only window";
		}
	}

	bool Window::isPresentationWindow(HWND hwnd)
	{
		return IsWindow(hwnd) && g_presentationWindowThreadId == GetWindowThreadProcessId(hwnd, nullptr);
	}

	bool Window::isTopLevelNonLayeredWindow(HWND hwnd)
	{
		return !(GetWindowLongPtr(hwnd, GWL_EXSTYLE) & WS_EX_LAYERED) &&
			(!(GetWindowLongPtr(hwnd, GWL_STYLE) & WS_CHILD) || GetParent(hwnd) == GetDesktopWindow() ||
				getComboLBoxAtom() == GetClassLong(hwnd, GCW_ATOM));
	}

	void Window::remove(HWND hwnd)
	{
		DDraw::ScopedThreadLock lock;
		s_windows.erase(hwnd);
	}

	void Window::uninstallHooks()
	{
		if (g_presentationWindowThread)
		{
			SendMessage(g_messageWindow, WM_CLOSE, 0, 0);
			if (WAIT_OBJECT_0 != WaitForSingleObject(g_presentationWindowThread, 1000))
			{
				TerminateThread(g_presentationWindowThread, 0);
				Compat::Log() << "The presentation window thread was terminated forcefully";
			}
		}
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
			}

			if (m_presentationWindow && GetCurrentThreadId() == GetWindowThreadProcessId(m_hwnd, nullptr))
			{
				SetWindowPos(m_presentationWindow, nullptr, newWindowRect.left, newWindowRect.top,
					newWindowRect.right - newWindowRect.left, newWindowRect.bottom - newWindowRect.top,
					SWP_SHOWWINDOW | SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_NOACTIVATE | SWP_NOSENDCHANGING | SWP_NOREDRAW);
			}
		}
		else if (m_presentationWindow && GetCurrentThreadId() == GetWindowThreadProcessId(m_hwnd, nullptr))
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
