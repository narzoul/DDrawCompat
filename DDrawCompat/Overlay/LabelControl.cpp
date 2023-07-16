#include <Common/Hook.h>
#include <Overlay/LabelControl.h>

namespace Overlay
{
	LabelControl::LabelControl(Control& parent, const RECT& rect, const std::string& label, UINT align, DWORD style)
		: Control(&parent, rect, style)
		, m_label(label)
		, m_wlabel(label.begin(), label.end())
		, m_align(align)
		, m_color(FOREGROUND_COLOR)
	{
	}

	void LabelControl::draw(HDC dc)
	{
		LONG x = 0;
		const LONG y = (m_rect.top + m_rect.bottom) / 2 - 7;

		switch (m_align)
		{
		case TA_LEFT:
			x = m_rect.left + BORDER;
			break;
		case TA_CENTER:
			x = (m_rect.left + m_rect.right) / 2;
			break;
		case TA_RIGHT:
			x = m_rect.right - BORDER;
			break;
		default:
			return;
		}

		auto prevColor = SetTextColor(dc, (FOREGROUND_COLOR == m_color && !isEnabled()) ? DISABLED_COLOR : m_color );
		auto prevTextAlign = SetTextAlign(dc, m_align);
		CALL_ORIG_FUNC(ExtTextOutW)(dc, x, y, ETO_IGNORELANGUAGE, nullptr, m_wlabel.c_str(), m_wlabel.length(), nullptr);
		SetTextAlign(dc, prevTextAlign);
		SetTextColor(dc, prevColor);
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

	void LabelControl::setLabel(const std::string label)
	{
		if (m_label != label)
		{
			m_label = label;
			m_wlabel.assign(label.begin(), label.end());
			invalidate();
		}
	}
}
