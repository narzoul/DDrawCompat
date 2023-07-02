#pragma once

#include <Common/Hook.h>
#include <Input/Input.h>
#include <Overlay/ScrollBarControl.h>

namespace
{
	const DWORD REPEAT_DELAY = 500;
	const DWORD REPEAT_INTERVAL = 100;

	UINT_PTR g_repeatTimerId = 0;

	long roundDiv(long n, long d)
	{
		return (n + d / 2) / d;
	}
}

namespace Overlay
{
	ScrollBarControl::ScrollBarControl(Control& parent, const RECT& rect, int min, int max)
		: Control(&parent, rect, WS_VISIBLE)
		, m_min(min)
		, m_max(max)
		, m_pos(min)
		, m_leftArrow{ rect.left, rect.top, rect.left + ARROW_SIZE, rect.bottom }
		, m_rightArrow{ rect.right - ARROW_SIZE, rect.top, rect.right, rect.bottom }
		, m_state(State::IDLE)
	{
	}

	void ScrollBarControl::draw(HDC dc)
	{
		drawArrow(dc, m_leftArrow, DFCS_SCROLLLEFT);
		drawArrow(dc, m_rightArrow, DFCS_SCROLLRIGHT);

		RECT r = { m_leftArrow.right, m_rect.top, m_rightArrow.left, m_rect.bottom };
		CALL_ORIG_FUNC(Rectangle)(dc, r.left - 1, r.top, r.right + 1, r.bottom);

		r = getThumbRect();
		SelectObject(dc, GetStockObject(DC_BRUSH));
		CALL_ORIG_FUNC(Ellipse)(dc, r.left, r.top, r.right, r.bottom);
		SelectObject(dc, GetStockObject(NULL_BRUSH));
	}

	int ScrollBarControl::getPageSize() const
	{
		return std::max((m_max - m_min) / 20, 1);
	}

	RECT ScrollBarControl::getThumbRect() const
	{
		const int thumbPos = (m_pos - m_min) * (m_rightArrow.left - m_leftArrow.right - ARROW_SIZE) / (m_max - m_min);
		return RECT{ m_leftArrow.right + thumbPos, m_rect.top, m_leftArrow.right + thumbPos + ARROW_SIZE, m_rect.bottom };
	}

	void ScrollBarControl::onLButtonDown(POINT pos)
	{
		Input::setCapture(this);
		
		if (PtInRect(&m_leftArrow, pos))
		{
			m_state = State::LEFT_ARROW_PRESSED;
		}
		else if (PtInRect(&m_rightArrow, pos))
		{
			m_state = State::RIGHT_ARROW_PRESSED;
		}
		else
		{
			RECT r = getThumbRect();
			if (PtInRect(&r, pos))
			{
				m_state = State::THUMB_PRESSED;
			}
			else if (pos.x < r.left)
			{
				m_state = State::LEFT_SHAFT_PRESSED;
			}
			else
			{
				m_state = State::RIGHT_SHAFT_PRESSED;
			}
		}

		scroll();

		if (State::THUMB_PRESSED != m_state)
		{
			startRepeatTimer(REPEAT_DELAY);
		}
	}

	void ScrollBarControl::onLButtonUp(POINT /*pos*/)
	{
		if (Input::getCapture() == this)
		{
			Input::setCapture(m_parent);
			stopRepeatTimer();
			m_state = State::IDLE;
		}
	}

	void ScrollBarControl::onMouseMove(POINT /*pos*/)
	{
		if (State::THUMB_PRESSED == m_state)
		{
			scroll();
		}
	}

	void ScrollBarControl::onRepeat()
	{
		stopRepeatTimer();
		scroll();
		startRepeatTimer(REPEAT_INTERVAL);
	}

	void CALLBACK ScrollBarControl::repeatTimerProc(HWND /*hwnd*/, UINT /*message*/, UINT_PTR /*iTimerID*/, DWORD /*dwTime*/)
	{
		static_cast<ScrollBarControl*>(Input::getCapture())->onRepeat();
	}

	void ScrollBarControl::scroll()
	{
		switch (m_state)
		{
		case State::LEFT_ARROW_PRESSED:
			setPos(m_pos - 1);
			break;

		case State::RIGHT_ARROW_PRESSED:
			setPos(m_pos + 1);
			break;

		case State::LEFT_SHAFT_PRESSED:
			if (Input::getRelativeCursorPos().x < getThumbRect().left)
			{
				setPos(m_pos - getPageSize());
			}
			break;

		case State::RIGHT_SHAFT_PRESSED:
			if (Input::getRelativeCursorPos().x >= getThumbRect().right)
			{
				setPos(m_pos + getPageSize());
			}
			break;

		case State::THUMB_PRESSED:
		{
			POINT pos = Input::getRelativeCursorPos();
			const auto minPos = m_leftArrow.right + ARROW_SIZE / 2;
			const auto maxPos = m_rightArrow.left - ARROW_SIZE / 2;
			pos.x = std::max(pos.x, minPos);
			pos.x = std::min(pos.x, maxPos);
			setPos(m_min + roundDiv((pos.x - minPos) * (m_max - m_min), maxPos - minPos));
			break;
		}
		}
	}

	void ScrollBarControl::setPos(int pos)
	{
		pos = std::max(pos, m_min);
		pos = std::min(pos, m_max);
		if (pos != m_pos)
		{
			m_pos = pos;
			m_parent->onNotify(*this);
		}
	}

	void ScrollBarControl::startRepeatTimer(DWORD time)
	{
		g_repeatTimerId = SetTimer(nullptr, g_repeatTimerId, time, &repeatTimerProc);
	}

	void ScrollBarControl::stopRepeatTimer()
	{
		if (0 != g_repeatTimerId)
		{
			KillTimer(nullptr, g_repeatTimerId);
			g_repeatTimerId = 0;
		}
	}
}
