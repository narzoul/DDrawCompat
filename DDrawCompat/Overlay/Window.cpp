#include <map>

#include <Windows.h>
#include <CommCtrl.h>

#include <Common/Hook.h>
#include <Common/Log.h>
#include <Dll/Dll.h>
#include <DDraw/RealPrimarySurface.h>
#include <Gdi/GuiThread.h>
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

	HFONT getDefaultFont()
	{
		static HFONT font = nullptr;
		if (!font)
		{
			LOGFONT lf = {};
			lf.lfHeight = 13;
			lf.lfWeight = FW_NORMAL;
			lf.lfQuality = NONANTIALIASED_QUALITY;
			strcpy_s(lf.lfFaceName, "Segoe UI");
			font = CreateFontIndirect(&lf);
		}
		return font;
	}

	void toggleWindow(void* window)
	{
		auto wnd = static_cast<Overlay::Window*>(window);
		wnd->setVisible(!wnd->isVisible());
	}
}

namespace Overlay
{
	Window::Window(Window* parentWindow, const RECT& rect, DWORD style, int alpha, const Input::HotKey& hotKey)
		: Control(nullptr, rect, style)
		, m_hwnd(Gdi::PresentationWindow::create(parentWindow ? parentWindow->m_hwnd : nullptr))
		, m_parentWindow(parentWindow)
		, m_alpha(alpha)
		, m_scaleFactor(1)
		, m_dc(CreateCompatibleDC(nullptr))
		, m_font(getDefaultFont())
		, m_bitmap(nullptr)
		, m_bitmapBits(nullptr)
		, m_invalid(true)
	{
		g_windows.emplace(m_hwnd, *this);
		CALL_ORIG_FUNC(SetWindowLongA)(m_hwnd, GWL_WNDPROC, reinterpret_cast<LONG>(&staticWindowProc));
		setAlpha(alpha);

		if (0 != hotKey.vk)
		{
			Input::registerHotKey(hotKey, &toggleWindow, this);
		}

		updateBitmap();

		SaveDC(m_dc);
	}

	Window::~Window()
	{
		Gdi::GuiThread::destroyWindow(m_hwnd);
		g_windows.erase(m_hwnd);
		RestoreDC(m_dc, -1);
		DeleteDC(m_dc);
		CALL_ORIG_FUNC(DeleteObject)(m_bitmap);
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

	void Window::setAlpha(int alpha)
	{
		m_alpha = alpha;
		CALL_ORIG_FUNC(SetLayeredWindowAttributes)(m_hwnd, 0, static_cast<BYTE>(alpha * 255 / 100), ULW_ALPHA);
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

		SelectObject(m_dc, m_font);
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

		m_invalid = false;
	}

	void Window::updateBitmap()
	{
		if (m_bitmap)
		{
			BITMAP bm = {};
			GetObject(m_bitmap, sizeof(BITMAP), &bm);
			if (bm.bmWidth == m_rect.right - m_rect.left &&
				bm.bmHeight == m_rect.bottom - m_rect.top)
			{
				return;
			}
			DeleteObject(m_bitmap);
			m_bitmap = nullptr;
		}

		if (IsRectEmpty(&m_rect))
		{
			return;
		}

		struct BITMAPINFO3 : public BITMAPINFO
		{
			RGBQUAD bmiRemainingColors[2];
		};

		BITMAPINFO3 bmi = {};
		bmi.bmiHeader.biSize = sizeof(bmi.bmiHeader);
		bmi.bmiHeader.biWidth = m_rect.right - m_rect.left;
		bmi.bmiHeader.biHeight = m_rect.top - m_rect.bottom;
		bmi.bmiHeader.biPlanes = 1;
		bmi.bmiHeader.biBitCount = 32;
		bmi.bmiHeader.biCompression = BI_BITFIELDS;
		reinterpret_cast<DWORD&>(bmi.bmiColors[0]) = 0xFF0000;
		reinterpret_cast<DWORD&>(bmi.bmiColors[1]) = 0x00FF00;
		reinterpret_cast<DWORD&>(bmi.bmiColors[2]) = 0x0000FF;

		m_bitmap = CreateDIBSection(nullptr, &bmi, DIB_RGB_COLORS, &m_bitmapBits, nullptr, 0);
		SelectObject(m_dc, m_bitmap);
	}

	void Window::updatePos()
	{
		const RECT monitorRect = Win32::DisplayMode::getMonitorInfo(GetForegroundWindow()).rcMonitor;
		int scaleX = (monitorRect.right - monitorRect.left) / VIRTUAL_SCREEN_WIDTH;
		int scaleY = (monitorRect.bottom - monitorRect.top) / VIRTUAL_SCREEN_HEIGHT;
		m_scaleFactor = std::min(scaleX, scaleY);
		m_scaleFactor = std::max(1, m_scaleFactor);
		m_rect = calculateRect({ monitorRect.left / m_scaleFactor, monitorRect.top / m_scaleFactor,
			monitorRect.right / m_scaleFactor, monitorRect.bottom / m_scaleFactor });

		{
			CALL_ORIG_FUNC(SetWindowPos)(m_hwnd, getTopmost(),
				m_rect.left * m_scaleFactor, m_rect.top * m_scaleFactor,
				(m_rect.right - m_rect.left) * m_scaleFactor, (m_rect.bottom - m_rect.top) * m_scaleFactor,
				SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_NOREDRAW | SWP_NOSENDCHANGING | SWP_SHOWWINDOW);
		}

		if (Input::getCaptureWindow() == this)
		{
			Input::setCapture(Input::getCapture());
		}

		updateBitmap();
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
