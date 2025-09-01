#include <Common/Hook.h>
#include <Overlay/LabelControl.h>
#include <Overlay/Window.h>

namespace Overlay
{
	LabelControl::LabelControl(Control& parent, const RECT& rect, const std::string& label, UINT align, DWORD style)
		: Control(&parent, rect, style)
		, m_label(label)
		, m_align(align)
		, m_color(FOREGROUND_COLOR)
	{
	}

	void LabelControl::draw(HDC dc)
	{
		if (m_label.empty())
		{
			return;
		}

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

		if (m_wlabel.empty())
		{
			updateWLabel();
		}

		auto prevColor = SetTextColor(dc, (FOREGROUND_COLOR == m_color && !isEnabled()) ? DISABLED_COLOR : m_color);
		auto prevTextAlign = SetTextAlign(dc, m_align);
		RECT r = { m_rect.left + BORDER, m_rect.top, m_rect.right - BORDER, m_rect.bottom };
		CALL_ORIG_FUNC(ExtTextOutW)(dc, x, y, ETO_IGNORELANGUAGE | ((m_style & WS_CLIPCHILDREN) ? ETO_CLIPPED : 0),
			&r, m_wlabel.c_str(), m_wlabel.length(), nullptr);
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

	void LabelControl::setLabel(const std::string& label)
	{
		if (m_label != label)
		{
			m_label = label;
			m_wlabel.clear();
			invalidate();
		}
	}

	void LabelControl::setRect(const RECT& rect)
	{
		m_rect = rect;
		const auto label = m_label;
		m_label.clear();
		setLabel(label);
	}

	void LabelControl::updateWLabel()
	{
		if (m_style & WS_CLIPCHILDREN)
		{
			HDC dc = CreateCompatibleDC(nullptr);
			auto origFont = SelectObject(dc, static_cast<Window&>(getRoot()).getFont());

			auto shortLabel(m_label);
			shortLabel.append(4, 0);
			RECT r = { m_rect.left + BORDER, m_rect.top, m_rect.right - BORDER, m_rect.bottom };
			CALL_ORIG_FUNC(DrawTextA)(dc, shortLabel.c_str(), m_label.length(), &r,
				DT_CALCRECT | DT_MODIFYSTRING | DT_PATH_ELLIPSIS | DT_SINGLELINE);
			m_wlabel.assign(shortLabel.begin(), shortLabel.begin() + strlen(shortLabel.c_str()));

			SelectObject(dc, origFont);
			DeleteDC(dc);
		}
		else
		{
			m_wlabel.assign(m_label.begin(), m_label.end());
		}
	}
}
