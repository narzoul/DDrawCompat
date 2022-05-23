#include <Common/Hook.h>
#include <Input/Input.h>
#include <Overlay/ButtonControl.h>

namespace Overlay
{
	ButtonControl::ButtonControl(Control& parent, const RECT& rect, const std::string& label, ClickHandler clickHandler)
		: Control(&parent, rect, WS_BORDER | WS_TABSTOP | WS_VISIBLE)
		, m_clickHandler(clickHandler)
		, m_label(*this, rect, label, DT_CENTER)
	{
	}

	void ButtonControl::onLButtonDown(POINT pos)
	{
		Input::setCapture(this);
		onMouseMove(pos);
	}

	void ButtonControl::onLButtonUp(POINT pos)
	{
		if (Input::getCapture() == this)
		{
			Input::setCapture(m_parent);
			m_label.setColor(FOREGROUND_COLOR);
			if (PtInRect(&m_rect, pos))
			{
				m_clickHandler(*this);
			}
			else
			{
				m_parent->onMouseMove(pos);
			}
		}
	}

	void ButtonControl::onMouseMove(POINT pos)
	{
		if (Input::getCapture() == this)
		{
			m_label.setColor(PtInRect(&m_rect, pos) ? HIGHLIGHT_COLOR : FOREGROUND_COLOR);
		}
		else
		{
			Control::onMouseMove(pos);
		}
	}
}
