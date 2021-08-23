#include <Common/Hook.h>
#include <Overlay/ComboBoxControl.h>

namespace Overlay
{
	ComboBoxControl::ComboBoxControl(Control& parent, const RECT& rect)
		: Control(&parent, rect, WS_BORDER | WS_VISIBLE)
		, m_dropDown(*this)
	{
	}

	void ComboBoxControl::setValue(const std::string& value)
	{
		m_value = value;
		invalidate(m_rect);
	}

	void ComboBoxControl::draw(HDC dc)
	{
		RECT rect = m_rect;
		rect.left = m_rect.right - 17;
		drawArrow(dc, rect, DFCS_SCROLLDOWN);

		rect.left = m_rect.left + BORDER;
		CALL_ORIG_FUNC(DrawTextA)(dc, m_value.c_str(), m_value.size(), &rect, DT_NOCLIP | DT_SINGLELINE | DT_VCENTER);
	}

	void ComboBoxControl::onLButtonDown(POINT /*pos*/)
	{
		m_dropDown.setVisible(true);
	}
}
