#include <algorithm>
#include <map>
#include <vector>

#include <dwmapi.h>

#include <Common/Hook.h>
#include <Common/Log.h>
#include <D3dDdi/KernelModeThunks.h>
#include <D3dDdi/ScopedCriticalSection.h>
#include <DDraw/RealPrimarySurface.h>
#include <Gdi/Gdi.h>
#include <Gdi/VirtualScreen.h>
#include <Gdi/Window.h>

namespace
{
	struct UpdateWindowContext
	{
		Gdi::Region obscuredRegion;
		Gdi::Region virtualScreenRegion;
		DWORD processId;
	};

	struct Window
	{
		HWND hwnd;
		HWND presentationWindow;
		RECT windowRect;
		Gdi::Region windowRegion;
		Gdi::Region visibleRegion;
		Gdi::Region invalidatedRegion;
		COLORREF colorKey;
		BYTE alpha;
		bool isLayered;
		bool isRgnChangePending;

		Window(HWND hwnd)
			: hwnd(hwnd)
			, presentationWindow(nullptr)
			, windowRect{}
			, windowRegion(nullptr)
			, colorKey(CLR_INVALID)
			, alpha(255)
			, isLayered(true)
			, isRgnChangePending(false)
		{
		}
	};

	const UINT WM_CREATEPRESENTATIONWINDOW = WM_USER;
	const UINT WM_SETPRESENTATIONWINDOWPOS = WM_USER + 1;
	const UINT WM_SETPRESENTATIONWINDOWRGN = WM_USER + 2;
	const RECT REGION_OVERRIDE_MARKER_RECT = { 32000, 32000, 32001, 32001 };

	HANDLE g_presentationWindowThread = nullptr;
	DWORD g_presentationWindowThreadId = 0;
	HWND g_messageWindow = nullptr;
	std::map<HWND, Window> g_windows;
	std::vector<Window*> g_windowZOrder;

	std::map<HWND, Window>::iterator addWindow(HWND hwnd)
	{
		DWMNCRENDERINGPOLICY ncRenderingPolicy = DWMNCRP_DISABLED;
		DwmSetWindowAttribute(hwnd, DWMWA_NCRENDERING_POLICY, &ncRenderingPolicy, sizeof(ncRenderingPolicy));

		BOOL disableTransitions = TRUE;
		DwmSetWindowAttribute(hwnd, DWMWA_TRANSITIONS_FORCEDISABLED, &disableTransitions, sizeof(disableTransitions));

		const auto style = GetClassLongPtr(hwnd, GCL_STYLE);
		if (style & CS_DROPSHADOW)
		{
			SetClassLongPtr(hwnd, GCL_STYLE, style & ~CS_DROPSHADOW);
		}

		return g_windows.emplace(hwnd, Window(hwnd)).first;
	}

	Gdi::Region getWindowRegion(HWND hwnd)
	{
		Gdi::Region rgn;
		if (ERROR == CALL_ORIG_FUNC(GetWindowRgn)(hwnd, rgn))
		{
			return nullptr;
		}
		return rgn;
	}

	LRESULT CALLBACK messageWindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
	{
		LOG_FUNC("messageWindowProc", Compat::WindowMessageStruct(hwnd, uMsg, wParam, lParam));
		switch (uMsg)
		{
		case WM_CREATEPRESENTATIONWINDOW:
		{
			// Workaround for ForceSimpleWindow shim
			static auto origCreateWindowExA = reinterpret_cast<decltype(&CreateWindowExA)>(
				Compat::getProcAddress(GetModuleHandle("user32"), "CreateWindowExA"));

			HWND owner = reinterpret_cast<HWND>(wParam);
			HWND presentationWindow = origCreateWindowExA(
				WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_NOPARENTNOTIFY | WS_EX_TOOLWINDOW,
				"DDrawCompatPresentationWindow",
				nullptr,
				WS_DISABLED | WS_POPUP,
				0, 0, 1, 1,
				owner,
				nullptr,
				nullptr,
				nullptr);

			if (presentationWindow)
			{
				CALL_ORIG_FUNC(SetLayeredWindowAttributes)(presentationWindow, 0, 255, LWA_ALPHA);
			}

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
		LOG_FUNC("presentationWindowProc", Compat::WindowMessageStruct(hwnd, uMsg, wParam, lParam));

		switch (uMsg)
		{
		case WM_SETPRESENTATIONWINDOWPOS:
		{
			const auto& wp = *reinterpret_cast<WINDOWPOS*>(lParam);
			return SetWindowPos(hwnd, wp.hwndInsertAfter, wp.x, wp.y, wp.cx, wp.cy, wp.flags);
		}

		case WM_SETPRESENTATIONWINDOWRGN:
		{
			HRGN rgn = nullptr;
			if (wParam)
			{
				rgn = CreateRectRgn(0, 0, 0, 0);
				CombineRgn(rgn, reinterpret_cast<HRGN>(wParam), nullptr, RGN_COPY);
			}
			return SetWindowRgn(hwnd, rgn, FALSE);
		}
		}

		return CALL_ORIG_FUNC(DefWindowProc)(hwnd, uMsg, wParam, lParam);
	}

	DWORD WINAPI presentationWindowThreadProc(LPVOID /*lpParameter*/)
	{
		SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);

		WNDCLASS wc = {};
		wc.lpfnWndProc = &messageWindowProc;
		wc.hInstance = Dll::g_currentModule;
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

	LRESULT sendMessageBlocking(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
	{
		DWORD_PTR result = 0;
		SendMessageTimeout(hwnd, msg, wParam, lParam, SMTO_BLOCK | SMTO_NOTIMEOUTIFNOTHUNG, 0, &result);
		return result;
	}

	void updatePosition(Window& window, const RECT& oldWindowRect, const Gdi::Region& oldVisibleRegion)
	{
		Gdi::Region preservedRegion(oldVisibleRegion);
		preservedRegion.offset(window.windowRect.left - oldWindowRect.left, window.windowRect.top - oldWindowRect.top);
		preservedRegion &= window.visibleRegion;

		POINT clientPos = {};
		ClientToScreen(window.hwnd, &clientPos);
		if (!preservedRegion.isEmpty())
		{
			Gdi::Region updateRegion;
			GetUpdateRgn(window.hwnd, updateRegion, FALSE);
			OffsetRgn(updateRegion, clientPos.x, clientPos.y);
			preservedRegion -= updateRegion;

			if (!preservedRegion.isEmpty() &&
				(window.windowRect.left != oldWindowRect.left || window.windowRect.top != oldWindowRect.top))
			{
				HDC screenDc = GetDC(nullptr);
				SelectClipRgn(screenDc, preservedRegion);
				BitBlt(screenDc, window.windowRect.left, window.windowRect.top,
					oldWindowRect.right - oldWindowRect.left, oldWindowRect.bottom - oldWindowRect.top,
					screenDc, oldWindowRect.left, oldWindowRect.top, SRCCOPY);
				SelectClipRgn(screenDc, nullptr);
				CALL_ORIG_FUNC(ReleaseDC)(nullptr, screenDc);
			}
		}

		window.invalidatedRegion = window.visibleRegion - preservedRegion;
		OffsetRgn(window.invalidatedRegion, -clientPos.x, -clientPos.y);
	}

	BOOL CALLBACK updateWindow(HWND hwnd, LPARAM lParam)
	{
		auto& context = *reinterpret_cast<UpdateWindowContext*>(lParam);

		DWORD processId = 0;
		GetWindowThreadProcessId(hwnd, &processId);
		if (GetWindowThreadProcessId(hwnd, &processId) == g_presentationWindowThreadId ||
			processId != context.processId)
		{
			return TRUE;
		}

		auto it = g_windows.find(hwnd);
		if (it == g_windows.end())
		{
			it = addWindow(hwnd);
		}
		g_windowZOrder.push_back(&it->second);

		const LONG exStyle = CALL_ORIG_FUNC(GetWindowLongA)(hwnd, GWL_EXSTYLE);
		const bool isLayered = exStyle & WS_EX_LAYERED;
		const bool isVisible = IsWindowVisible(hwnd) && !IsIconic(hwnd);
		bool setPresentationWindowRgn = false;

		if (isLayered != it->second.isLayered)
		{
			it->second.isLayered = isLayered;
			it->second.isRgnChangePending = isVisible;
			if (!isLayered)
			{
				it->second.presentationWindow = reinterpret_cast<HWND>(
					sendMessageBlocking(g_messageWindow, WM_CREATEPRESENTATIONWINDOW, reinterpret_cast<WPARAM>(hwnd), 0));
				setPresentationWindowRgn = true;
			}
			else if (it->second.presentationWindow)
			{
				sendMessageBlocking(it->second.presentationWindow, WM_CLOSE, 0, 0);
				it->second.presentationWindow = nullptr;
			}
		}

		Gdi::Region windowRegion(getWindowRegion(hwnd));
		if (windowRegion && !PtInRegion(windowRegion, REGION_OVERRIDE_MARKER_RECT.left, REGION_OVERRIDE_MARKER_RECT.top) ||
			!windowRegion && it->second.windowRegion)
		{
			swap(it->second.windowRegion, windowRegion);
			setPresentationWindowRgn = true;
		}

		RECT windowRect = {};
		Gdi::Region visibleRegion;

		if (isVisible)
		{
			GetWindowRect(hwnd, &windowRect);
			if (!IsRectEmpty(&windowRect))
			{
				if (it->second.windowRegion)
				{
					visibleRegion = it->second.windowRegion;
					OffsetRgn(visibleRegion, windowRect.left, windowRect.top);
				}
				else
				{
					visibleRegion = windowRect;
				}
				visibleRegion &= context.virtualScreenRegion;
				visibleRegion -= context.obscuredRegion;

				if (!isLayered && !(exStyle & WS_EX_TRANSPARENT))
				{
					context.obscuredRegion |= visibleRegion;
				}
			}
		}

		std::swap(it->second.windowRect, windowRect);
		swap(it->second.visibleRegion, visibleRegion);

		if (!isLayered)
		{
			if (!it->second.visibleRegion.isEmpty())
			{
				updatePosition(it->second, windowRect, visibleRegion);
			}

			if (isVisible && !it->second.isRgnChangePending)
			{
				OffsetRgn(visibleRegion, it->second.windowRect.left - windowRect.left, it->second.windowRect.top - windowRect.top);
				if (it->second.visibleRegion != visibleRegion)
				{
					it->second.isRgnChangePending = true;
				}
			}

			if (it->second.presentationWindow)
			{
				if (setPresentationWindowRgn)
				{
					HRGN rgn = it->second.windowRegion;
					sendMessageBlocking(it->second.presentationWindow, WM_SETPRESENTATIONWINDOWRGN,
						reinterpret_cast<WPARAM>(rgn), 0);
				}

				WINDOWPOS wp = {};
				wp.flags = SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_NOREDRAW | SWP_NOSENDCHANGING;
				if (isVisible)
				{
					wp.hwndInsertAfter = GetWindow(hwnd, GW_HWNDPREV);
					if (!wp.hwndInsertAfter)
					{
						wp.hwndInsertAfter = (GetWindowLong(hwnd, GWL_EXSTYLE) & WS_EX_TOPMOST) ? HWND_TOPMOST : HWND_TOP;
					}
					else if (wp.hwndInsertAfter == it->second.presentationWindow)
					{
						wp.flags |= SWP_NOZORDER;
					}

					wp.x = it->second.windowRect.left;
					wp.y = it->second.windowRect.top;
					wp.cx = it->second.windowRect.right - it->second.windowRect.left;
					wp.cy = it->second.windowRect.bottom - it->second.windowRect.top;
					wp.flags |= SWP_SHOWWINDOW;
				}
				else
				{
					wp.flags |= SWP_HIDEWINDOW | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER;
				}

				sendMessageBlocking(it->second.presentationWindow, WM_SETPRESENTATIONWINDOWPOS,
					0, reinterpret_cast<LPARAM>(&wp));
			}
		}
		return TRUE;
	}
}

namespace Gdi
{
	namespace Window
	{
		void installHooks()
		{
			WNDCLASS wc = {};
			wc.lpfnWndProc = &presentationWindowProc;
			wc.hInstance = Dll::g_currentModule;
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
				Compat::Log() << "ERROR: Failed to create a message-only window";
			}
		}

		bool isPresentationWindow(HWND hwnd)
		{
			return g_presentationWindowThreadId == GetWindowThreadProcessId(hwnd, nullptr);
		}

		void onStyleChanged(HWND hwnd, WPARAM wParam)
		{
			if (GWL_EXSTYLE == wParam)
			{
				D3dDdi::ScopedCriticalSection lock;
				auto it = g_windows.find(hwnd);
				if (it != g_windows.end())
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
			bool isInvalidated = false;

			{
				D3dDdi::ScopedCriticalSection lock;
				auto it = g_windows.find(hwnd);
				if (it == g_windows.end())
				{
					return;
				}

				if (it->second.isRgnChangePending)
				{
					it->second.isRgnChangePending = false;
					const LONG origWndProc = CALL_ORIG_FUNC(GetWindowLongA)(hwnd, GWL_WNDPROC);
					CALL_ORIG_FUNC(SetWindowLongA)(hwnd, GWL_WNDPROC, reinterpret_cast<LONG>(CALL_ORIG_FUNC(DefWindowProcA)));
					if (it->second.isLayered)
					{
						SetWindowRgn(hwnd, Gdi::Region(it->second.windowRegion).release(), FALSE);
					}
					else
					{
						Gdi::Region rgn(it->second.visibleRegion);
						OffsetRgn(rgn, -it->second.windowRect.left, -it->second.windowRect.top);
						rgn |= REGION_OVERRIDE_MARKER_RECT;
						SetWindowRgn(hwnd, rgn, FALSE);
					}
					CALL_ORIG_FUNC(SetWindowLongA)(hwnd, GWL_WNDPROC, origWndProc);
				}

				isInvalidated = !it->second.invalidatedRegion.isEmpty();
				if (isInvalidated)
				{
					RedrawWindow(hwnd, nullptr, it->second.invalidatedRegion,
						RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN);
					it->second.invalidatedRegion = nullptr;
				}
			}

			if (isInvalidated)
			{
				RECT emptyRect = {};
				RedrawWindow(hwnd, &emptyRect, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ERASENOW);
			}
		}

		void present(CompatRef<IDirectDrawSurface7> dst, CompatRef<IDirectDrawSurface7> src,
			CompatRef<IDirectDrawClipper> clipper)
		{
			D3dDdi::ScopedCriticalSection lock;
			for (auto window : g_windowZOrder)
			{
				if (window->presentationWindow && !window->visibleRegion.isEmpty())
				{
					clipper->SetHWnd(&clipper, 0, window->presentationWindow);
					dst->Blt(&dst, nullptr, &src, nullptr, DDBLT_WAIT, nullptr);
				}
			}
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
					virtualScreenDc.reset(Gdi::VirtualScreen::createDc());
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

		void presentLayered(CompatRef<IDirectDrawSurface7> dst, POINT offset)
		{
			D3dDdi::ScopedCriticalSection lock;

			HDC dstDc = nullptr;
			for (auto it = g_windowZOrder.rbegin(); it != g_windowZOrder.rend(); ++it)
			{
				auto& window = **it;
				if (!window.isLayered)
				{
					continue;
				}

				if (!dstDc)
				{
					const UINT D3DDDIFMT_UNKNOWN = 0;
					const UINT D3DDDIFMT_X8R8G8B8 = 22;
					D3dDdi::KernelModeThunks::setDcFormatOverride(D3DDDIFMT_X8R8G8B8);
					dst->GetDC(&dst, &dstDc);
					D3dDdi::KernelModeThunks::setDcFormatOverride(D3DDDIFMT_UNKNOWN);
					if (!dstDc)
					{
						return;
					}
				}

				HDC windowDc = GetWindowDC(window.hwnd);
				Gdi::Region rgn(window.visibleRegion);
				RECT wr = window.windowRect;

				if (0 != offset.x || 0 != offset.y)
				{
					OffsetRect(&wr, offset.x, offset.y);
					rgn.offset(offset.x, offset.y);
				}

				SelectClipRgn(dstDc, rgn);

				auto colorKey = window.colorKey;
				if (CLR_INVALID != colorKey)
				{
					CALL_ORIG_FUNC(TransparentBlt)(dstDc, wr.left, wr.top, wr.right - wr.left, wr.bottom - wr.top,
						windowDc, 0, 0, wr.right - wr.left, wr.bottom - wr.top, colorKey);
				}
				else
				{
					BLENDFUNCTION blend = {};
					blend.SourceConstantAlpha = window.alpha;
					CALL_ORIG_FUNC(AlphaBlend)(dstDc, wr.left, wr.top, wr.right - wr.left, wr.bottom - wr.top,
						windowDc, 0, 0, wr.right - wr.left, wr.bottom - wr.top, blend);
				}

				CALL_ORIG_FUNC(ReleaseDC)(window.hwnd, windowDc);
			}

			if (dstDc)
			{
				SelectClipRgn(dstDc, nullptr);
				dst->ReleaseDC(&dst, dstDc);
			}
		}

		void updateAll()
		{
			LOG_FUNC("Window::updateAll");
			UpdateWindowContext context;
			context.processId = GetCurrentProcessId();
			context.virtualScreenRegion = VirtualScreen::getRegion();
			std::vector<HWND> invalidatedWindows;

			{
				D3dDdi::ScopedCriticalSection lock;
				g_windowZOrder.clear();
				EnumWindows(updateWindow, reinterpret_cast<LPARAM>(&context));

				for (auto it = g_windows.begin(); it != g_windows.end();)
				{
					if (g_windowZOrder.end() == std::find(g_windowZOrder.begin(), g_windowZOrder.end(), &it->second))
					{
						if (it->second.presentationWindow)
						{
							sendMessageBlocking(it->second.presentationWindow, WM_CLOSE, 0, 0);
						}
						it = g_windows.erase(it);
					}
					else
					{
						++it;
					}
				}

				for (auto it = g_windowZOrder.rbegin(); it != g_windowZOrder.rend(); ++it)
				{
					auto& window = **it;
					if (window.isRgnChangePending || !window.invalidatedRegion.isEmpty())
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

		void updateLayeredWindowInfo(HWND hwnd, COLORREF colorKey, BYTE alpha)
		{
			D3dDdi::ScopedCriticalSection lock;
			auto it = g_windows.find(hwnd);
			if (it != g_windows.end())
			{
				it->second.colorKey = colorKey;
				it->second.alpha = alpha;
				if (!it->second.visibleRegion.isEmpty())
				{
					DDraw::RealPrimarySurface::scheduleUpdate();
				}
			}
		}

		void uninstallHooks()
		{
			if (g_presentationWindowThread)
			{
				sendMessageBlocking(g_messageWindow, WM_CLOSE, 0, 0);
				if (WAIT_OBJECT_0 != WaitForSingleObject(g_presentationWindowThread, 1000))
				{
					TerminateThread(g_presentationWindowThread, 0);
					Compat::Log() << "The presentation window thread was terminated forcefully";
				}
			}
		}
	}
}
