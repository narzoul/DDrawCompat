#include <Common/Hook.h>
#include <Overlay/LabelControl.h>

namespace Overlay
{
	LabelControl::LabelControl(Control& parent, const RECT& rect, const std::string& label, UINT format, DWORD style)
		: Control(&parent, rect, style)
		, m_label(label)
		, m_format(format)
		, m_color(FOREGROUND_COLOR)
	{
	}

	void LabelControl::draw(HDC dc)
	{
		RECT r = { m_rect.left + BORDER, m_rect.top, m_rect.right - BORDER, m_rect.bottom };
		SetTextColor(dc, m_color);
		CALL_ORIG_FUNC(DrawTextA)(dc, m_label.c_str(), m_label.size(), &r,
			m_format | DT_NOCLIP | DT_SINGLELINE | DT_VCENTER);
		SetTextColor(dc, FOREGROUND_COLOR);
	}

	void LabelControl::onLButtonDown(POINT /*pos*/)
	{
		m_parent->onNotify(*this);
	}

	void LabelControl::setColor(COLORREF color)
	{
		if (m_color != color)
		{
			m_color = color;
			invalidate();
		}
	}
}
