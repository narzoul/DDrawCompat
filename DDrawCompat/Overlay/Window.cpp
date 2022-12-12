#include <map>

#include <Windows.h>
#include <CommCtrl.h>

#include <Common/Hook.h>
#include <Common/Log.h>
#include <Dll/Dll.h>
#include <DDraw/RealPrimarySurface.h>
#include <DDraw/Surfaces/PrimarySurface.h>
#include <Gdi/GuiThread.h>
#include <Gdi/PresentationWindow.h>
#include <Input/Input.h>
#include <Overlay/Control.h>
#include <Overlay/Window.h>

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
	Window::Window(Window* parentWindow, const RECT& rect, DWORD style, const Input::HotKey& hotKey)
		: Control(nullptr, rect, style)
		, m_hwnd(Gdi::PresentationWindow::create(parentWindow ? parentWindow->m_hwnd : nullptr))
		, m_parentWindow(parentWindow)
		, m_transparency(25)
		, m_scaleFactor(1)
		, m_dc(CreateCompatibleDC(nullptr))
		, m_bitmap(nullptr)
		, m_bitmapBits(nullptr)
		, m_invalid(true)
	{
		g_windows.emplace(m_hwnd, *this);
		CALL_ORIG_FUNC(SetWindowLongA)(m_hwnd, GWL_WNDPROC, reinterpret_cast<LONG>(&staticWindowProc));
		setTransparency(m_transparency);

		if (0 != hotKey.vk)
		{
			Input::registerHotKey(hotKey, &toggleWindow, this);
		}

		struct BITMAPINFO3 : public BITMAPINFO
		{
			RGBQUAD bmiRemainingColors[2];
		};

		BITMAPINFO3 bmi = {};
		bmi.bmiHeader.biSize = sizeof(bmi.bmiHeader);
		bmi.bmiHeader.biWidth = rect.right - rect.left;
		bmi.bmiHeader.biHeight = rect.top - rect.bottom;
		bmi.bmiHeader.biPlanes = 1;
		bmi.bmiHeader.biBitCount = 32;
		bmi.bmiHeader.biCompression = BI_BITFIELDS;
		reinterpret_cast<DWORD&>(bmi.bmiColors[0]) = 0xFF0000;
		reinterpret_cast<DWORD&>(bmi.bmiColors[1]) = 0x00FF00;
		reinterpret_cast<DWORD&>(bmi.bmiColors[2]) = 0x0000FF;

		m_bitmap = CreateDIBSection(nullptr, &bmi, DIB_RGB_COLORS, &m_bitmapBits, nullptr, 0);
		SaveDC(m_dc);
		SelectObject(m_dc, m_bitmap);
	}

	Window::~Window()
	{
		Gdi::GuiThread::destroyWindow(m_hwnd);
		g_windows.erase(m_hwnd);
		RestoreDC(m_dc, -1);
		DeleteDC(m_dc);
		DeleteObject(m_bitmap);
	}

	void Window::draw(HDC /*dc*/)
	{
	}

	HWND Window::getTopmost() const
	{
		return DDraw::RealPrimarySurface::getTopmost();
	}

	void Window::invalidate()
	{
		m_invalid = true;
		DDraw::RealPrimarySurface::scheduleOverlayUpdate();
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
		}
		else
		{
			auto capture = Input::getCaptureWindow();
			if (capture && capture != this && capture->m_parentWindow == this)
			{
				capture->setVisible(false);
			}
			ShowWindow(m_hwnd, SW_HIDE);
		}
		DDraw::RealPrimarySurface::scheduleOverlayUpdate();
	}

	LRESULT CALLBACK Window::staticWindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
	{
		auto it = g_windows.find(hwnd);
		if (it != g_windows.end())
		{
			return it->second.windowProc(uMsg, wParam, lParam);
		}
		return CALL_ORIG_FUNC(DefWindowProcA)(hwnd, uMsg, wParam, lParam);
	}

	void Window::update()
	{
		if (!m_invalid || !isVisible())
		{
			return;
		}
		m_invalid = false;

		static HFONT font = createDefaultFont();

		SelectObject(m_dc, font);
		SelectObject(m_dc, GetStockObject(DC_PEN));
		SelectObject(m_dc, GetStockObject(NULL_BRUSH));
		SetBkColor(m_dc, RGB(0, 0, 0));
		SetDCBrushColor(m_dc, RGB(0, 0, 0));
		SetDCPenColor(m_dc, FOREGROUND_COLOR);
		SetTextColor(m_dc, FOREGROUND_COLOR);

		RECT rect = { 0, 0, m_rect.right - m_rect.left, m_rect.bottom - m_rect.top };
		CALL_ORIG_FUNC(FillRect)(m_dc, &rect, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
		drawAll(m_dc);

		HDC windowDc = GetWindowDC(m_hwnd);
		CALL_ORIG_FUNC(StretchBlt)(
			windowDc, 0, 0, (m_rect.right - m_rect.left) * m_scaleFactor, (m_rect.bottom - m_rect.top) * m_scaleFactor,
			m_dc, 0, 0, rect.right - rect.left, rect.bottom - rect.top, SRCCOPY);
		ReleaseDC(m_hwnd, windowDc);
	}

	void Window::updatePos()
	{
		RECT monitorRect = DDraw::RealPrimarySurface::getMonitorRect();
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

		int scaleX = (monitorRect.right - monitorRect.left) / 640;
		int scaleY = (monitorRect.bottom - monitorRect.top) / 480;
		m_scaleFactor = min(scaleX, scaleY);
		m_scaleFactor = max(1, m_scaleFactor);
		m_rect = calculateRect({ monitorRect.left / m_scaleFactor, monitorRect.top / m_scaleFactor,
			monitorRect.right / m_scaleFactor, monitorRect.bottom / m_scaleFactor });

		CALL_ORIG_FUNC(SetWindowPos)(m_hwnd, getTopmost(),
			m_rect.left * m_scaleFactor, m_rect.top * m_scaleFactor,
			(m_rect.right - m_rect.left) * m_scaleFactor, (m_rect.bottom - m_rect.top) * m_scaleFactor,
			SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_NOREDRAW | SWP_NOSENDCHANGING | SWP_SHOWWINDOW);

		if (Input::getCaptureWindow() == this)
		{
			Input::setCapture(Input::getCapture());
		}

		invalidate();
	}

	LRESULT Window::windowProc(UINT uMsg, WPARAM wParam, LPARAM lParam)
	{
		switch (uMsg)
		{
		case WM_DISPLAYCHANGE:
			if (m_style & WS_VISIBLE)
			{
				updatePos();
			}
			break;
		}

		return CALL_ORIG_FUNC(DefWindowProcA)(m_hwnd, uMsg, wParam, lParam);
	}
}
