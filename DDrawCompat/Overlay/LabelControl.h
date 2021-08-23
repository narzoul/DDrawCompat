#pragma once

#include <string>

#include <Windows.h>

#include <Overlay/Control.h>

namespace Overlay
{
	class LabelControl : public Control
	{
	public:
		LabelControl(Control& parent, const RECT& rect, const std::string& label, UINT format);

		virtual void onLButtonDown(POINT pos) override;

		std::string getLabel() const { return m_label; }

	private:
		virtual void draw(HDC dc) override;

		std::string m_label;
		UINT m_format;
	};
}
