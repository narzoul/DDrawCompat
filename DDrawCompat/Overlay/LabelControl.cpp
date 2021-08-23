#include <Common/Hook.h>
#include <Overlay/LabelControl.h>

namespace Overlay
{
	LabelControl::LabelControl(Control& parent, const RECT& rect, const std::string& label, UINT format)
		: Control(&parent, rect, WS_VISIBLE)
		, m_label(label)
		, m_format(format)
	{
	}

	void LabelControl::draw(HDC dc)
	{
		RECT r = { m_rect.left + BORDER, m_rect.top, m_rect.right - BORDER, m_rect.bottom };
		CALL_ORIG_FUNC(DrawTextA)(dc, m_label.c_str(), m_label.size(), &r,
			m_format | DT_NOCLIP | DT_SINGLELINE | DT_VCENTER);
	}

	void LabelControl::onLButtonDown(POINT /*pos*/)
	{
		m_parent->onNotify(*this);
	}
}
