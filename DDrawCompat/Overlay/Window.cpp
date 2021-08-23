#include <map>

#include <Windows.h>
#include <CommCtrl.h>

#include <Common/Hook.h>
#include <Common/Log.h>
#include <Config/Config.h>
#include <Dll/Dll.h>
#include <DDraw/RealPrimarySurface.h>
#include <DDraw/Surfaces/PrimarySurface.h>
#include <Gdi/PresentationWindow.h>
#include <Input/Input.h>
#include <Overlay/Control.h>
#include <Overlay/Window.h>
#include <Win32/DisplayMode.h>

namespace
{
	enum class ControlType
	{
		COMBOBOX,
		SLIDER
	};

	std::map<HWND, Overlay::Window&> g_windows;

	HFONT createDefaultFont()
	{
		LOGFONT lf = {};
		lf.lfHeight = 13;
		lf.lfWeight = FW_NORMAL;
		lf.lfQuality = NONANTIALIASED_QUALITY;
		strcpy_s(lf.lfFaceName, "Segoe UI");
		return CreateFontIndirect(&lf);
	}

	void toggleWindow(void* window)
	{
		auto wnd = static_cast<Overlay::Window*>(window);
		wnd->setVisible(!wnd->isVisible());
	}
}

namespace Overlay
{
	Window::Window(Window* parentWindow, const RECT& rect, const Input::HotKey& hotKey)
		: Control(nullptr, rect, WS_BORDER)
		, m_hwnd(Gdi::PresentationWindow::create(parentWindow ? parentWindow->m_hwnd : nullptr, &staticWindowProc))
		, m_parentWindow(parentWindow)
		, m_transparency(25)
	{
		g_windows.emplace(m_hwnd, *this);
		setTransparency(m_transparency);
		if (0 != hotKey.vk)
		{
			Input::registerHotKey(hotKey, &toggleWindow, this);
		}
	}

	Window::~Window()
	{
		Gdi::PresentationWindow::destroy(m_hwnd);
		g_windows.erase(m_hwnd);
	}

	void Window::draw(HDC /*dc*/)
	{
	}

	void Window::invalidate(const RECT& rect)
	{
		InvalidateRect(m_hwnd, &rect, TRUE);
	}

	void Window::onEraseBackground(HDC dc)
	{
		RECT r = { 0, 0, m_rect.right - m_rect.left, m_rect.bottom - m_rect.top };
		CALL_ORIG_FUNC(FillRect)(dc, &r, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
	}

	void Window::onPaint()
	{
		static HFONT font = createDefaultFont();

		PAINTSTRUCT ps = {};
		HDC dc = BeginPaint(m_hwnd, &ps);
		SelectObject(dc, font);
		SelectObject(dc, GetStockObject(DC_PEN));
		SelectObject(dc, GetStockObject(NULL_BRUSH));
		SetBkColor(dc, RGB(0, 0, 0));
		SetDCBrushColor(dc, RGB(0, 0, 0));
		SetDCPenColor(dc, RGB(0, 255, 0));
		SetTextColor(dc, RGB(0, 255, 0));

		drawAll(dc);

		EndPaint(m_hwnd, &ps);
		DDraw::RealPrimarySurface::scheduleUpdate();
	}

	void Window::setTransparency(int transparency)
	{
		m_transparency = transparency;
		CALL_ORIG_FUNC(SetLayeredWindowAttributes)(m_hwnd, 0, static_cast<BYTE>(100 - transparency) * 255 / 100, ULW_ALPHA);
	}

	void Window::setVisible(bool isVisible)
	{
		if (isVisible == Window::isVisible())
		{
			return;
		}

		m_style ^= WS_VISIBLE;
		if (m_style & WS_VISIBLE)
		{
			updatePos();
			ShowWindow(m_hwnd, SW_SHOWNA);
			Input::setCapture(this);
		}
		else
		{
			auto capture = Input::getCapture();
			if (capture != this && capture->m_parentWindow == this)
			{
				capture->setVisible(false);
			}
			ShowWindow(m_hwnd, SW_HIDE);
			Input::setCapture(m_parentWindow);
		}
	}

	LRESULT CALLBACK Window::staticWindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
	{
		return g_windows.find(hwnd)->second.windowProc(uMsg, wParam, lParam);
	}

	void Window::updatePos()
	{
		auto monitorRect = Win32::DisplayMode::getEmulatedDisplayMode().rect;
		if (IsRectEmpty(&monitorRect))
		{
			monitorRect = DDraw::PrimarySurface::getMonitorRect();
			if (IsRectEmpty(&monitorRect))
			{
				HMONITOR monitor = nullptr;
				HWND foregroundWindow = GetForegroundWindow();
				if (foregroundWindow)
				{
					monitor = MonitorFromWindow(foregroundWindow, MONITOR_DEFAULTTONEAREST);
				}
				else
				{
					monitor = MonitorFromPoint({ 0, 0 }, MONITOR_DEFAULTTOPRIMARY);
				}

				MONITORINFO mi = {};
				mi.cbSize = sizeof(mi);
				CALL_ORIG_FUNC(GetMonitorInfoA)(monitor, &mi);
				monitorRect = mi.rcMonitor;

				if (IsRectEmpty(&monitorRect))
				{
					monitorRect = { 0, 0, m_rect.right - m_rect.left, m_rect.bottom - m_rect.top };
				}
			}
		}

		m_rect = calculateRect(monitorRect);
		CALL_ORIG_FUNC(SetWindowPos)(m_hwnd, HWND_TOPMOST, m_rect.left, m_rect.top,
			m_rect.right - m_rect.left, m_rect.bottom - m_rect.top, SWP_NOACTIVATE);
	}

	LRESULT Window::windowProc(UINT uMsg, WPARAM wParam, LPARAM lParam)
	{
		switch (uMsg)
		{
		case WM_DISPLAYCHANGE:
			updatePos();
			break;

		case WM_ERASEBKGND:
			onEraseBackground(reinterpret_cast<HDC>(wParam));
			return 1;

		case WM_PAINT:
			onPaint();
			return 0;
		}

		return CALL_ORIG_FUNC(DefWindowProcA)(m_hwnd, uMsg, wParam, lParam);
	}
}
