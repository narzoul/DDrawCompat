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
	ScrollBarControl::ScrollBarControl(Control& parent, const RECT& rect, int min, int max, int pos, DWORD style)
		: Control(&parent, rect, style)
		, m_min(min)
		, m_max(std::max(min, max))
		, m_pos(std::max(min, std::min(max, pos)))
		, m_pageSize(0)
		, m_state(State::IDLE)
		, m_left(isHorizontal() ? &RECT::left : &RECT::top)
		, m_top(isHorizontal() ? &RECT::top : &RECT::left)
		, m_right(isHorizontal() ? &RECT::right : &RECT::bottom)
		, m_bottom(isHorizontal() ? &RECT::bottom : &RECT::right)
		, m_x(isHorizontal() ? &POINT::x : &POINT::y)
	{
	}

	void ScrollBarControl::draw(HDC dc)
	{
		drawArrow(dc, getLeftArrowRect(), isHorizontal() ? DFCS_SCROLLLEFT : DFCS_SCROLLUP);
		drawArrow(dc, getRightArrowRect(), isHorizontal() ? DFCS_SCROLLRIGHT : DFCS_SCROLLDOWN);

		RECT r = m_rect;
		r.*m_left += ARROW_SIZE - 1;
		r.*m_right -= ARROW_SIZE - 1;
		CALL_ORIG_FUNC(Rectangle)(dc, r.left, r.top, r.right, r.bottom);

		r = getThumbRect();
		SelectObject(dc, GetStockObject(DC_BRUSH));
		CALL_ORIG_FUNC(Ellipse)(dc, r.left, r.top, r.right, r.bottom);
		SelectObject(dc, GetStockObject(NULL_BRUSH));
	}

	RECT ScrollBarControl::getLeftArrowRect() const
	{
		RECT r = m_rect;
		r.*m_right = r.*m_left + ARROW_SIZE;
		return r;
	}

	RECT ScrollBarControl::getRightArrowRect() const
	{
		RECT r = m_rect;
		r.*m_left = r.*m_right - ARROW_SIZE;
		return r;
	}

	int ScrollBarControl::getPageSize() const
	{
		if (m_pageSize > 0)
		{
			return m_pageSize;
		}
		return std::max((m_max - m_min) / 20, 1);
	}

	RECT ScrollBarControl::getThumbRect() const
	{
		const int thumbPos = (m_pos - m_min) * (m_rect.*m_right - m_rect.*m_left - 3 * ARROW_SIZE) / std::max(m_max - m_min, 1);
		RECT r = m_rect;
		r.*m_left = m_rect.*m_left + ARROW_SIZE + thumbPos;
		r.*m_right = r.*m_left + ARROW_SIZE;
		return r;
	}

	bool ScrollBarControl::isHorizontal() const
	{
		return m_rect.right - m_rect.left > m_rect.bottom - m_rect.top;
	}

	void ScrollBarControl::onLButtonDown(POINT pos)
	{
		Input::setCapture(this);

		if (pos.*m_x < m_rect.*m_left + ARROW_SIZE)
		{
			m_state = State::LEFT_ARROW_PRESSED;
		}
		else if (pos.*m_x >= m_rect.*m_right - ARROW_SIZE)
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
			else if (pos.*m_x < r.*m_left)
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

	void ScrollBarControl::onMouseWheel(POINT /*pos*/, SHORT delta)
	{
		if (State::IDLE == m_state)
		{
			setPos(m_pos - delta / WHEEL_DELTA * getPageSize());
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
			if (Input::getRelativeCursorPos().*m_x < getThumbRect().*m_left)
			{
				setPos(m_pos - getPageSize());
			}
			break;

		case State::RIGHT_SHAFT_PRESSED:
			if (Input::getRelativeCursorPos().*m_x >= getThumbRect().*m_right)
			{
				setPos(m_pos + getPageSize());
			}
			break;

		case State::THUMB_PRESSED:
		{
			auto pos = Input::getRelativeCursorPos().*m_x;
			const auto minPos = m_rect.*m_left + ARROW_SIZE + ARROW_SIZE / 2;
			const auto maxPos = m_rect.*m_right - ARROW_SIZE - ARROW_SIZE / 2;
			pos = std::max(pos, minPos);
			pos = std::min(pos, maxPos);
			setPos(m_min + roundDiv((pos - minPos) * (m_max - m_min), maxPos - minPos));
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
			m_parent->invalidate();
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
