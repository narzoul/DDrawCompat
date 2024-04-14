#include <Common/Hook.h>
#include <Common/Log.h>
#include <Dll/Dll.h>
#include <DDraw/RealPrimarySurface.h>
#include <Gdi/GuiThread.h>
#include <Gdi/PresentationWindow.h>

namespace
{
	ATOM g_classAtom = 0;

	std::wstring getWindowText(HWND hwnd)
	{
		const UINT MAX_LEN = 256;
		wchar_t windowText[MAX_LEN] = {};
		InternalGetWindowText(hwnd, windowText, MAX_LEN);
		return windowText;
	}

	LRESULT CALLBACK presentationWindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
	{
		LOG_FUNC("presentationWindowProc", Compat::WindowMessageStruct(hwnd, uMsg, wParam, lParam));
		if (WM_NULL == uMsg && WM_GETTEXT == wParam && WM_SETTEXT == lParam)
		{
			std::wstring windowText(L"[DDrawCompat] " + getWindowText(GetParent(hwnd)));
			SetWindowTextW(hwnd, windowText.c_str());
			return LOG_RESULT(0);
		}
		return LOG_RESULT(CALL_ORIG_FUNC(DefWindowProcA)(hwnd, uMsg, wParam, lParam));
	}
}

namespace Gdi
{
	namespace PresentationWindow
	{
		HWND create(HWND owner, bool dpiAware)
		{
			LOG_FUNC("PresentationWindow::create", owner, dpiAware);
			HWND presentationWindow = nullptr;
			GuiThread::execute([&]()
				{
					presentationWindow = GuiThread::createWindow(
						WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_NOPARENTNOTIFY | WS_EX_NOACTIVATE | (owner ? 0 : WS_EX_TOOLWINDOW),
						reinterpret_cast<const wchar_t*>(g_classAtom),
						nullptr,
						WS_DISABLED | WS_POPUP,
						0, 0, 1, 1,
						owner,
						nullptr,
						nullptr,
						nullptr,
						dpiAware);

					if (presentationWindow)
					{
						CALL_ORIG_FUNC(SetLayeredWindowAttributes)(presentationWindow, 0, 255, LWA_ALPHA);
						if (owner)
						{
							AttachThreadInput(GetCurrentThreadId(), GetWindowThreadProcessId(owner, nullptr), FALSE);
						}
						DDraw::RealPrimarySurface::scheduleOverlayUpdate();
					}
				});
			return LOG_RESULT(presentationWindow);
		}

		void installHooks()
		{
			WNDCLASS wc = {};
			wc.lpfnWndProc = &presentationWindowProc;
			wc.hInstance = Dll::g_currentModule;
			wc.hCursor = CALL_ORIG_FUNC(LoadCursorA)(nullptr, IDC_ARROW);
			wc.lpszClassName = "DDrawCompatPresentationWindow";
			g_classAtom = CALL_ORIG_FUNC(RegisterClassA)(&wc);
		}
	}
}
