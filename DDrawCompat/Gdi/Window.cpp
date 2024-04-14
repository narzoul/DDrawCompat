#include <algorithm>
#include <map>
#include <vector>

#include <Common/Log.h>
#include <Common/Rect.h>
#include <D3dDdi/KernelModeThunks.h>
#include <D3dDdi/ScopedCriticalSection.h>
#include <DDraw/RealPrimarySurface.h>
#include <DDraw/Surfaces/PrimarySurface.h>
#include <Gdi/Gdi.h>
#include <Gdi/GuiThread.h>
#include <Gdi/PresentationWindow.h>
#include <Gdi/VirtualScreen.h>
#include <Gdi/Window.h>
#include <Gdi/WinProc.h>
#include <Input/Input.h>
#include <Overlay/ConfigWindow.h>
#include <Overlay/StatsWindow.h>
#include <Win32/DisplayMode.h>
#include <Win32/DpiAwareness.h>

namespace
{
	class LayeredWindowContent
	{
	public:
		~LayeredWindowContent()
		{
			destroy();
		}

		BYTE getAlpha() const { return m_alpha; }
		BYTE getAlphaFormat() const { return m_alphaFormat; }
		COLORREF getColorKey() const { return m_colorKey; }
		HDC getDc() const { return m_dc; }

		void set(HWND hwnd, HDC hdcSrc, const POINT* pptSrc, COLORREF colorKey, BYTE alpha, BYTE alphaFormat)
		{
			if (hdcSrc)
			{
				RECT wr = {};
				GetWindowRect(hwnd, &wr);
				const SIZE size = Rect::getSize(wr);

				if (size != m_size)
				{
					destroy();
					m_size = size;
					m_dc = CreateCompatibleDC(nullptr);
					m_origBmp = SelectObject(m_dc, CALL_ORIG_FUNC(CreateBitmap)(size.cx, size.cy, 1, 32, nullptr));
				}

				CALL_ORIG_FUNC(BitBlt)(m_dc, 0, 0, size.cx, size.cy,
					hdcSrc, pptSrc ? pptSrc->x : 0, pptSrc ? pptSrc->y : 0, SRCCOPY);

				if (alphaFormat & AC_SRC_ALPHA)
				{
					BITMAP bm = {};
					GetObject(GetCurrentObject(hdcSrc, OBJ_BITMAP), sizeof(bm), &bm);
					if (32 != bm.bmBitsPixel)
					{
						alphaFormat &= ~AC_SRC_ALPHA;
					}
				}
			}

			m_colorKey = colorKey;
			m_alpha = alpha;
			m_alphaFormat = alphaFormat;
		}

	private:
		void destroy()
		{
			if (m_dc)
			{
				DeleteObject(SelectObject(m_dc, m_origBmp));
				DeleteDC(m_dc);
				m_dc = nullptr;
				m_origBmp = nullptr;
			}
		}

		HDC m_dc = nullptr;
		HGDIOBJ m_origBmp = nullptr;
		SIZE m_size = {};
		COLORREF m_colorKey = CLR_INVALID;
		BYTE m_alpha = 255;
		BYTE m_alphaFormat = 0;
	};

	struct UpdateWindowContext
	{
		Gdi::Region obscuredRegion;
		Gdi::Region invalidatedRegion;
		Gdi::Region virtualScreenRegion;
		DWORD processId;
	};

	struct Window
	{
		HWND hwnd;
		HWND presentationWindow;
		RECT windowRect;
		RECT clientRect;
		Gdi::Region visibleRegion;
		Gdi::Region invalidatedRegion;
		LayeredWindowContent layeredWindowContent;
		bool isMenu;
		bool isLayered;
		bool isDpiAware;

		Window(HWND hwnd)
			: hwnd(hwnd)
			, presentationWindow(nullptr)
			, windowRect{}
			, clientRect{}
			, isMenu(Gdi::MENU_ATOM == GetClassLong(hwnd, GCW_ATOM))
			, isLayered(true)
			, isDpiAware(false)
		{
		}
	};

	std::map<HWND, Window> g_windows;
	std::vector<Window*> g_windowZOrder;
	HWND g_fullscreenWindow = nullptr;

	bool bltWindow(const RECT& dst, const RECT& src, const Gdi::Region& clipRegion)
	{
		if (dst.left == src.left && dst.top == src.top || clipRegion.isEmpty())
		{
			return false;
		}

		HDC screenDc = GetDC(nullptr);
		SelectClipRgn(screenDc, clipRegion);
		BitBlt(screenDc, dst.left, dst.top, src.right - src.left, src.bottom - src.top, screenDc, src.left, src.top, SRCCOPY);
		SelectClipRgn(screenDc, nullptr);
		ReleaseDC(nullptr, screenDc);
		return true;
	}

	HWND findFullscreenWindow()
	{
		D3dDdi::ScopedCriticalSection lock;
		auto allMi = Win32::DisplayMode::getAllMonitorInfo();
		for (auto& window : g_windows)
		{
			for (const auto& mi : allMi)
				if (!window.second.isLayered &&
					window.second.windowRect.left <= mi.second.rcEmulated.left &&
					window.second.windowRect.top <= mi.second.rcEmulated.top &&
					window.second.windowRect.right >= mi.second.rcEmulated.right &&
					window.second.windowRect.bottom >= mi.second.rcEmulated.bottom)
				{
					return window.first;
				}
		}
		return nullptr;
	}

	auto removeWindow(std::map<HWND, Window>::iterator it)
	{
		if (it->second.presentationWindow)
		{
			Gdi::GuiThread::destroyWindow(it->second.presentationWindow);
			it->second.presentationWindow = nullptr;
		}
		return g_windows.erase(it);
	}

	void updatePosition(Window& window, const RECT& oldWindowRect, const RECT& oldClientRect,
		const Gdi::Region& oldVisibleRegion, Gdi::Region& invalidatedRegion)
	{
		LOG_FUNC("Window::updatePosition", window.hwnd, oldWindowRect, oldClientRect,
			static_cast<HRGN>(oldVisibleRegion), static_cast<HRGN>(invalidatedRegion));

		const bool isClientOriginChanged =
			window.clientRect.left - window.windowRect.left != oldClientRect.left - oldWindowRect.left ||
			window.clientRect.top - window.windowRect.top != oldClientRect.top - oldWindowRect.top;
		const bool isClientWidthChanged =
			window.clientRect.right - window.clientRect.left != oldClientRect.right - oldClientRect.left;
		const bool isClientHeightChanged =
			window.clientRect.bottom - window.clientRect.top != oldClientRect.bottom - oldClientRect.top;
		const bool isClientInvalidated =
			isClientWidthChanged && (GetClassLong(window.hwnd, GCL_STYLE) & CS_HREDRAW) ||
			isClientHeightChanged && (GetClassLong(window.hwnd, GCL_STYLE) & CS_VREDRAW);
		const bool isFrameInvalidated = isClientOriginChanged || isClientWidthChanged || isClientHeightChanged ||
			window.windowRect.right - window.windowRect.left != oldWindowRect.right - oldWindowRect.left ||
			window.windowRect.bottom - window.windowRect.top != oldWindowRect.bottom - oldWindowRect.top;

		Gdi::Region preservedRegion;
		if (!isClientInvalidated || !isFrameInvalidated)
		{
			preservedRegion = oldVisibleRegion - invalidatedRegion;
			preservedRegion.offset(window.windowRect.left - oldWindowRect.left, window.windowRect.top - oldWindowRect.top);
			preservedRegion &= window.visibleRegion;

			if (isClientInvalidated)
			{
				preservedRegion -= window.clientRect;
			}
			else
			{
				if (isFrameInvalidated)
				{
					preservedRegion &= window.clientRect;
				}

				if (isClientWidthChanged)
				{
					RECT r = window.clientRect;
					r.left += oldClientRect.right - oldClientRect.left;
					if (r.left < r.right)
					{
						preservedRegion -= r;
					}
				}
				if (isClientHeightChanged)
				{
					RECT r = window.clientRect;
					r.top += oldClientRect.bottom - oldClientRect.top;
					if (r.top < r.bottom)
					{
						preservedRegion -= r;
					}
				}

				Gdi::Region updateRegion;
				GetUpdateRgn(window.hwnd, updateRegion, FALSE);
				updateRegion.offset(window.clientRect.left, window.clientRect.top);
				preservedRegion -= updateRegion;
			}

			bool isCopied = false;
			if (!isFrameInvalidated)
			{
				isCopied = bltWindow(window.windowRect, oldWindowRect, preservedRegion);
			}
			else
			{
				isCopied = bltWindow(window.clientRect, oldClientRect, preservedRegion);
			}

			if (isCopied)
			{
				invalidatedRegion |= preservedRegion;
			}
		}

		window.invalidatedRegion = window.visibleRegion - preservedRegion;
		if (!window.invalidatedRegion.isEmpty())
		{
			window.invalidatedRegion.offset(-window.clientRect.left, -window.clientRect.top);
		}
	}

	BOOL CALLBACK updateWindow(HWND hwnd, LPARAM lParam)
	{
		auto& context = *reinterpret_cast<UpdateWindowContext*>(lParam);

		DWORD processId = 0;
		GetWindowThreadProcessId(hwnd, &processId);
		if (processId != context.processId ||
			Gdi::GuiThread::isGuiThreadWindow(hwnd))
		{
			return TRUE;
		}

		const bool isWindowVisible = IsWindowVisible(hwnd);
		const bool isVisible = isWindowVisible && !IsIconic(hwnd);
		auto it = g_windows.find(hwnd);
		if (it == g_windows.end())
		{
			if (!isWindowVisible)
			{
				return TRUE;
			}
			it = g_windows.emplace(hwnd, Window(hwnd)).first;
		}

		const LONG exStyle = CALL_ORIG_FUNC(GetWindowLongA)(hwnd, GWL_EXSTYLE);
		const bool isLayered = it->second.isMenu || (exStyle & WS_EX_LAYERED);

		if (isLayered != it->second.isLayered)
		{
			it->second.isLayered = isLayered;
			if (!isLayered)
			{
				it->second.presentationWindow = Gdi::PresentationWindow::create(hwnd);
				Gdi::Window::updatePresentationWindowText(it->second.presentationWindow);
			}
			else if (it->second.presentationWindow)
			{
				Gdi::GuiThread::destroyWindow(it->second.presentationWindow);
				it->second.presentationWindow = nullptr;
			}
		}

		WINDOWINFO wi = {};
		Gdi::Region visibleRegion;

		if (isVisible)
		{
			wi.cbSize = sizeof(wi);
			GetWindowInfo(hwnd, &wi);
			if (!IsRectEmpty(&wi.rcWindow))
			{
				if (isLayered)
				{
					if (ERROR != GetWindowRgn(hwnd, visibleRegion))
					{
						visibleRegion.offset(wi.rcWindow.left, wi.rcWindow.top);
					}
					else
					{
						visibleRegion = wi.rcWindow;
					}
				}
				else
				{
					HDC windowDc = GetWindowDC(hwnd);
					CALL_ORIG_FUNC(GetRandomRgn)(windowDc, visibleRegion, SYSRGN);
					ReleaseDC(hwnd, windowDc);
				}

				visibleRegion &= context.virtualScreenRegion;
				if (!it->second.isMenu)
				{
					visibleRegion -= context.obscuredRegion;
				}

				if (!isLayered && !(exStyle & WS_EX_TRANSPARENT))
				{
					context.obscuredRegion |= visibleRegion;
				}
			}
		}

		std::swap(it->second.windowRect, wi.rcWindow);
		std::swap(it->second.clientRect, wi.rcClient);
		swap(it->second.visibleRegion, visibleRegion);

		if (!isLayered)
		{
			if (!it->second.visibleRegion.isEmpty())
			{
				updatePosition(it->second, wi.rcWindow, wi.rcClient, visibleRegion, context.invalidatedRegion);
			}

			if (exStyle & WS_EX_TRANSPARENT)
			{
				context.invalidatedRegion |= visibleRegion - it->second.visibleRegion;
			}

			if (it->second.presentationWindow && isVisible &&
				it->second.presentationWindow != DDraw::RealPrimarySurface::getPresentationWindow())
			{
				Gdi::GuiThread::setWindowRgn(it->second.presentationWindow, Gdi::Window::getWindowRgn(hwnd));
				Gdi::Window::updatePresentationWindowPos(it->second.presentationWindow, hwnd);
			}
		}

		if (isWindowVisible)
		{
			g_windowZOrder.push_back(&it->second);
		}
		else
		{
			removeWindow(it);
		}
		return TRUE;
	}
}

namespace Gdi
{
	namespace Window
	{
		void destroyWindow(HWND hwnd)
		{
			D3dDdi::ScopedCriticalSection lock;
			auto it = g_windows.find(hwnd);
			if (it == g_windows.end())
			{
				return;
			}

			if (it->second.visibleRegion.isEmpty())
			{
				removeWindow(it);
			}
			else
			{
				updateAll();
			}
		}

		HWND getPresentationWindow(HWND hwnd)
		{
			D3dDdi::ScopedCriticalSection lock;
			auto it = g_windows.find(hwnd);
			return it != g_windows.end() ? it->second.presentationWindow : nullptr;
		}

		std::vector<LayeredWindow> getVisibleLayeredWindows()
		{
			std::vector<LayeredWindow> layeredWindows;
			for (auto it = g_windowZOrder.rbegin(); it != g_windowZOrder.rend(); ++it)
			{
				auto& window = **it;
				if (window.isLayered && !window.visibleRegion.isEmpty())
				{
					layeredWindows.push_back({
						window.hwnd,
						window.windowRect,
						window.visibleRegion,
						window.layeredWindowContent.getDc(),
						window.layeredWindowContent.getColorKey(),
						window.layeredWindowContent.getAlpha(),
						window.layeredWindowContent.getAlphaFormat() });
				}
			}
			return layeredWindows;
		}

		std::vector<LayeredWindow> getVisibleOverlayWindows()
		{
			std::vector<LayeredWindow> layeredWindows;
			RECT wr = {};
			auto statsWindow = GuiThread::getStatsWindow();
			if (statsWindow && statsWindow->isVisible())
			{
				GetWindowRect(statsWindow->getWindow(), &wr);
				auto visibleRegion(getWindowRgn(statsWindow->getWindow()));
				visibleRegion.offset(wr.left, wr.top);
				layeredWindows.push_back({ statsWindow->getWindow(), wr, visibleRegion });
			}

			auto configWindow = GuiThread::getConfigWindow();
			if (configWindow && configWindow->isVisible())
			{
				GetWindowRect(configWindow->getWindow(), &wr);
				auto visibleRegion(getWindowRgn(configWindow->getWindow()));
				visibleRegion.offset(wr.left, wr.top);
				layeredWindows.push_back({ configWindow->getWindow(), wr, visibleRegion });
				auto capture = Input::getCaptureWindow();
				if (capture && capture != configWindow)
				{
					GetWindowRect(capture->getWindow(), &wr);
					layeredWindows.push_back({ capture->getWindow(), wr, nullptr });
				}
			}

			HWND cursorWindow = Input::getCursorWindow();
			if (cursorWindow)
			{
				GetWindowRect(cursorWindow, &wr);
				layeredWindows.push_back({ cursorWindow, wr, nullptr });
			}

			return layeredWindows;
		}

		HWND getFullscreenWindow()
		{
			D3dDdi::ScopedCriticalSection lock;
			return g_fullscreenWindow;
		}

		int getRandomRgn(HDC hdc, HRGN hrgn, INT i)
		{
			auto result = CALL_ORIG_FUNC(GetRandomRgn)(hdc, hrgn, i);
			if (1 != result || SYSRGN != i)
			{
				return result;
			}

			HWND hwnd = WindowFromDC(hdc);
			if (!hwnd)
			{
				return result;
			}

			HWND root = GetAncestor(hwnd, GA_ROOT);
			if (!root)
			{
				return result;
			}

			D3dDdi::ScopedCriticalSection lock;
			auto it = g_windows.find(root);
			if (it == g_windows.end())
			{
				return result;
			}

			CombineRgn(hrgn, hrgn, it->second.visibleRegion, RGN_AND);
			return result;
		}

		Gdi::Region getWindowRgn(HWND hwnd)
		{
			Gdi::Region rgn;
			if (ERROR == CALL_ORIG_FUNC(GetWindowRgn)(hwnd, rgn))
			{
				return nullptr;
			}
			return rgn;
		}

		bool isTopLevelWindow(HWND hwnd)
		{
			return GetDesktopWindow() == GetAncestor(hwnd, GA_PARENT);
		}

		void onStyleChanged(HWND hwnd, WPARAM wParam)
		{
			if (GWL_EXSTYLE == wParam)
			{
				D3dDdi::ScopedCriticalSection lock;
				auto it = g_windows.find(hwnd);
				if (it != g_windows.end() && !it->second.isMenu)
				{
					const bool isLayered = GetWindowLong(hwnd, GWL_EXSTYLE) & WS_EX_LAYERED;
					if (isLayered != it->second.isLayered)
					{
						updateAll();
					}
				}
			}
		}

		void onSyncPaint(HWND hwnd)
		{
			LOG_FUNC("Window::onSyncPaint", hwnd);

			{
				D3dDdi::ScopedCriticalSection lock;
				auto it = g_windows.find(hwnd);
				if (it == g_windows.end())
				{
					return;
				}

				RedrawWindow(hwnd, nullptr, it->second.invalidatedRegion,
					RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN);
				it->second.invalidatedRegion.clear();
			}

			RECT emptyRect = {};
			RedrawWindow(hwnd, &emptyRect, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ERASENOW);
		}

		void present(Gdi::Region excludeRegion)
		{
			D3dDdi::ScopedCriticalSection lock;
			std::unique_ptr<HDC__, void(*)(HDC)> virtualScreenDc(nullptr, &Gdi::VirtualScreen::deleteDc);
			RECT virtualScreenBounds = Gdi::VirtualScreen::getBounds();

			for (auto window : g_windowZOrder)
			{
				if (!window->presentationWindow)
				{
					continue;
				}

				Gdi::Region visibleRegion(window->visibleRegion);
				if (excludeRegion)
				{
					visibleRegion -= excludeRegion;
				}
				if (visibleRegion.isEmpty())
				{
					continue;
				}

				if (!virtualScreenDc)
				{
					const bool useDefaultPalette = false;
					virtualScreenDc.reset(Gdi::VirtualScreen::createDc(useDefaultPalette));
					if (!virtualScreenDc)
					{
						return;
					}
				}

				HDC dc = GetWindowDC(window->presentationWindow);
				RECT rect = window->windowRect;
				visibleRegion.offset(-rect.left, -rect.top);
				SelectClipRgn(dc, visibleRegion);
				CALL_ORIG_FUNC(BitBlt)(dc, 0, 0, rect.right - rect.left, rect.bottom - rect.top, virtualScreenDc.get(),
					rect.left - virtualScreenBounds.left, rect.top - virtualScreenBounds.top, SRCCOPY);
				CALL_ORIG_FUNC(ReleaseDC)(window->presentationWindow, dc);
			}
		}

		void updateAll()
		{
			LOG_FUNC("Window::updateAll");
			if (!GuiThread::isReady())
			{
				return;
			}

			UpdateWindowContext context;
			context.processId = GetCurrentProcessId();
			context.virtualScreenRegion = VirtualScreen::getBounds();
			std::vector<HWND> invalidatedWindows;

			{
				D3dDdi::ScopedCriticalSection lock;
				g_windowZOrder.clear();
				CALL_ORIG_FUNC(EnumWindows)(updateWindow, reinterpret_cast<LPARAM>(&context));

				for (auto it = g_windows.begin(); it != g_windows.end();)
				{
					if (g_windowZOrder.end() == std::find(g_windowZOrder.begin(), g_windowZOrder.end(), &it->second))
					{
						it = removeWindow(it);
					}
					else
					{
						++it;
					}
				}

				g_fullscreenWindow = findFullscreenWindow();

				for (auto it = g_windowZOrder.rbegin(); it != g_windowZOrder.rend(); ++it)
				{
					auto& window = **it;
					if (!window.invalidatedRegion.isEmpty())
					{
						invalidatedWindows.push_back(window.hwnd);
					}
				}
			}

			for (auto hwnd : invalidatedWindows)
			{
				SendNotifyMessage(hwnd, WM_SYNCPAINT, 0, 0);
			}
		}

		void setDpiAwareness(HWND hwnd, bool dpiAware)
		{
			if (!Win32::DpiAwareness::isMixedModeSupported())
			{
				return;
			}

			D3dDdi::ScopedCriticalSection lock;
			auto it = g_windows.find(hwnd);
			if (it != g_windows.end() && it->second.isDpiAware != dpiAware)
			{
				it->second.isDpiAware = dpiAware;
				auto prevPresentationWindow = it->second.presentationWindow;
				it->second.presentationWindow = Gdi::PresentationWindow::create(hwnd, dpiAware);

				if (it->second.presentationWindow)
				{
					updatePresentationWindowText(it->second.presentationWindow);
					Gdi::GuiThread::setWindowRgn(it->second.presentationWindow, getWindowRgn(hwnd));
				}

				Gdi::GuiThread::destroyWindow(prevPresentationWindow);
			}
		}

		void updateLayeredWindowInfo(HWND hwnd, HDC hdcSrc, const POINT* pptSrc,
			COLORREF colorKey, BYTE alpha, BYTE alphaFormat)
		{
			D3dDdi::ScopedCriticalSection lock;
			auto it = g_windows.find(hwnd);
			if (it != g_windows.end())
			{
				it->second.layeredWindowContent.set(hwnd, hdcSrc, pptSrc, colorKey, alpha, alphaFormat);
				if (DDraw::RealPrimarySurface::isFullscreen())
				{
					DDraw::RealPrimarySurface::scheduleOverlayUpdate();
				}
			}
		}

		void updatePresentationWindowPos(HWND presentationWindow, HWND owner)
		{
			if (IsIconic(owner))
			{
				return;
			}

			WINDOWPOS wp = {};
			wp.flags = SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_NOREDRAW | SWP_NOSENDCHANGING;
			if (IsWindowVisible(owner))
			{
				wp.hwndInsertAfter = CALL_ORIG_FUNC(GetWindow)(owner, GW_HWNDPREV);
				if (!wp.hwndInsertAfter)
				{
					wp.hwndInsertAfter = (GetWindowLong(owner, GWL_EXSTYLE) & WS_EX_TOPMOST) ? HWND_TOPMOST : HWND_TOP;
				}
				else if (wp.hwndInsertAfter == presentationWindow)
				{
					wp.flags |= SWP_NOZORDER;
				}

				RECT wr = {};
				GetWindowRect(owner, &wr);

				wp.x = wr.left;
				wp.y = wr.top;
				wp.cx = wr.right - wr.left;
				wp.cy = wr.bottom - wr.top;
				wp.flags |= SWP_SHOWWINDOW;
			}
			else
			{
				wp.flags |= SWP_HIDEWINDOW | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER;
			}

			Gdi::GuiThread::execute([&]()
				{
					CALL_ORIG_FUNC(SetWindowPos)(presentationWindow,
						wp.hwndInsertAfter, wp.x, wp.y, wp.cx, wp.cy, wp.flags | SWP_NOMOVE);
					CALL_ORIG_FUNC(SetWindowPos)(presentationWindow,
						wp.hwndInsertAfter, wp.x, wp.y, wp.cx, wp.cy, wp.flags | SWP_NOSIZE);
				});
		}

		void updatePresentationWindowText(HWND presentationWindow)
		{
			PostMessageW(presentationWindow, WM_NULL, WM_GETTEXT, WM_SETTEXT);
		}

		void updateWindowPos(HWND hwnd)
		{
			if (IsWindowVisible(hwnd) && !IsIconic(hwnd))
			{
				updateAll();
				return;
			}

			D3dDdi::ScopedCriticalSection lock;
			auto it = g_windows.find(hwnd);
			if (it != g_windows.end() && !it->second.visibleRegion.isEmpty())
			{
				updateAll();
			}
		}
	}
}
