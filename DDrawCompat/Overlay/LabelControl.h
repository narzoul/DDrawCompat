#pragma once

#include <string>

#include <Windows.h>

#include <Overlay/Control.h>

namespace Overlay
{
	class LabelControl : public Control
	{
	public:
		LabelControl(Control& parent, const RECT& rect, const std::string& label, UINT format, DWORD style = WS_VISIBLE);

		virtual void onLButtonDown(POINT pos) override;

		const std::string& getLabel() const { return m_label; }
		void setLabel(const std::string label) { m_label = label; }
		void setColor(COLORREF color);

	private:
		virtual void draw(HDC dc) override;

		std::string m_label;
		UINT m_format;
		COLORREF m_color;
	};
}
