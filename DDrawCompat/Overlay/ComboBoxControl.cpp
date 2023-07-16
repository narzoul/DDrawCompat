#include <Common/Hook.h>
#include <Input/Input.h>
#include <Overlay/ComboBoxControl.h>

namespace Overlay
{
	ComboBoxControl::ComboBoxControl(Control& parent, const RECT& rect, const std::vector<std::string>& values)
		: Control(&parent, rect, WS_BORDER | WS_VISIBLE)
		, m_label(*this, { rect.left, rect.top, rect.right - ARROW_SIZE, rect.bottom }, std::string(), 0)
		, m_dropDown(*this, values)
	{
	}

	void ComboBoxControl::setValue(const std::string& value)
	{
		m_label.setLabel(value);
		m_dropDown.select(value);
	}

	void ComboBoxControl::draw(HDC dc)
	{
		RECT rect = m_rect;
		rect.left = m_label.getRect().right;
		drawArrow(dc, rect, DFCS_SCROLLDOWN);
	}

	void ComboBoxControl::onLButtonDown(POINT /*pos*/)
	{
		m_dropDown.setVisible(true);
	}
}
