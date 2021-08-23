#pragma once

#include <vector>

#include <Windows.h>

#include <Input/Input.h>
#include <Overlay/Control.h>

namespace Overlay
{
	class Window : public Control
	{
	public:
		Window(Window* parentWindow, const RECT& rect, const Input::HotKey& hotKey = {});
		virtual ~Window() override;

		virtual RECT calculateRect(const RECT& monitorRect) const = 0;
		virtual void invalidate(const RECT& rect) override;
		virtual void setVisible(bool isVisible) override;

		HWND getWindow() const { return m_hwnd; }
		void setTransparency(int transparency);

	protected:
		HWND m_hwnd;
		Window* m_parentWindow;
		int m_transparency;

		void updatePos();

	private:
		virtual void draw(HDC dc) override;

		void onEraseBackground(HDC dc);
		void onPaint();
		LRESULT windowProc(UINT uMsg, WPARAM wParam, LPARAM lParam);

		static LRESULT CALLBACK staticWindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
	};
}
