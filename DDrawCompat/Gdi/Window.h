#pragma once

#include <ddraw.h>

#include <Common/CompatRef.h>
#include <Gdi/Region.h>

namespace Gdi
{
	namespace Window
	{
		struct LayeredWindow
		{
			HWND hwnd;
			RECT rect;
			Gdi::Region region;
			HDC dc;
			COLORREF colorKey;
			BYTE alpha;
			BYTE alphaFormat;
		};

		void destroyWindow(HWND hwnd);
		HWND getPresentationWindow(HWND hwnd);
		std::vector<LayeredWindow> getVisibleLayeredWindows();
		std::vector<LayeredWindow> getVisibleOverlayWindows();
		HWND getFullscreenWindow();
		int getRandomRgn(HDC hdc, HRGN hrgn, INT i);
		Gdi::Region getWindowRgn(HWND hwnd);
		bool isTopLevelWindow(HWND hwnd);
		void onStyleChanged(HWND hwnd, WPARAM wParam);
		void present(CompatRef<IDirectDrawSurface7> dst, CompatRef<IDirectDrawSurface7> src,
			CompatRef<IDirectDrawClipper> clipper);
		void present(Gdi::Region excludeRegion);
		void setDpiAwareness(HWND hwnd, bool dpiAware);
		void setFullscreenMode(bool isFullscreen);
		void updateAll();
		void updateLayeredWindowInfo(HWND hwnd, HDC hdcSrc, const POINT* pptSrc,
			COLORREF colorKey, BYTE alpha, BYTE alphaFormat);
		void updatePresentationWindowPos(HWND presentationWindow, HWND owner);
		void updatePresentationWindowText(HWND presentationWindow);
		void updateWindowPos(HWND hwnd);
	}
}
