#pragma once

#include <vector>

#include <Windows.h>

#include <Input/Input.h>
#include <Input/HotKey.h>
#include <Overlay/Control.h>

namespace Overlay
{
	class Window : public Control
	{
	public:
		static const LONG VIRTUAL_SCREEN_WIDTH = 640;
		static const LONG VIRTUAL_SCREEN_HEIGHT = 480;

		Window(Window* parentWindow, const RECT& rect, DWORD style, int alpha, const Input::HotKey& hotKey = {});
		virtual ~Window() override;

		virtual RECT calculateRect(const RECT& monitorRect) const = 0;
		virtual void invalidate() override;
		virtual void setVisible(bool isVisible) override;

		int getScaleFactor() const { return m_scaleFactor; }
		HWND getWindow() const { return m_hwnd; }
		void setAlpha(int alpha);
		void update();
		void updatePos();

	protected:
		HWND m_hwnd;
		Window* m_parentWindow;
		int m_alpha;

		virtual HWND getTopmost() const;

	private:
		virtual void draw(HDC dc) override;

		LRESULT windowProc(UINT uMsg, WPARAM wParam, LPARAM lParam);

		static LRESULT CALLBACK staticWindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

		int m_scaleFactor;
		HDC m_dc;
		HBITMAP m_bitmap;
		void* m_bitmapBits;
		bool m_invalid;
	};
}
