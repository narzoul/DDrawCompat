#pragma once

#include <string>

#include <Windows.h>

#include <Overlay/Control.h>

namespace Overlay
{
	class LabelControl : public Control
	{
	public:
		LabelControl(Control& parent, const RECT& rect, const std::string& label, UINT align, DWORD style = WS_VISIBLE);

		virtual void draw(HDC dc) override;
		virtual void onLButtonDown(POINT pos) override;
		virtual void setRect(const RECT& rect) override;

		COLORREF getColor() const { return m_color; }
		const std::string& getLabel() const { return m_label; }
		void setColor(COLORREF color);
		void setLabel(const std::string& label);

	private:
		void updateWLabel();

		std::string m_label;
		std::wstring m_wlabel;
		UINT m_align;
		COLORREF m_color;
	};
}
