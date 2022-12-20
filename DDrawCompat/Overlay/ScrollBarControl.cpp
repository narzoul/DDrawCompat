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

		const int thumbPos = (m_pos - m_min) * (r.right - r.left - ARROW_SIZE) / (m_max - m_min);
		r = { m_leftArrow.right + thumbPos, r.top, m_leftArrow.right + thumbPos + ARROW_SIZE, r.bottom };
		SelectObject(dc, GetStockObject(DC_BRUSH));
		CALL_ORIG_FUNC(Ellipse)(dc, r.left, r.top, r.right, r.bottom);
		SelectObject(dc, GetStockObject(NULL_BRUSH));
	}

	void ScrollBarControl::onLButtonDown(POINT pos)
	{
		Input::setCapture(this);
		if (PtInRect(&m_leftArrow, pos))
		{
			setPos(m_pos - 1);
			if (State::IDLE == m_state)
			{
				m_state = State::LEFT_ARROW_PRESSED;
				startRepeatTimer(REPEAT_DELAY);
			}
		}
		else if (PtInRect(&m_rightArrow, pos))
		{
			setPos(m_pos + 1);
			if (State::IDLE == m_state)
			{
				m_state = State::RIGHT_ARROW_PRESSED;
				startRepeatTimer(REPEAT_DELAY);
			}
		}
		else
		{
			onThumbTrack(pos);
			if (State::IDLE == m_state)
			{
				m_state = State::THUMB_TRACKING;
			}
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

	void ScrollBarControl::onMouseMove(POINT pos)
	{
		if (State::THUMB_TRACKING == m_state)
		{
			onThumbTrack(pos);
		}
	}

	void ScrollBarControl::onRepeat()
	{
		stopRepeatTimer();

		switch (m_state)
		{
		case State::LEFT_ARROW_PRESSED:
			setPos(m_pos - 1);
			startRepeatTimer(REPEAT_INTERVAL);
			break;

		case State::RIGHT_ARROW_PRESSED:
			setPos(m_pos + 1);
			startRepeatTimer(REPEAT_INTERVAL);
			break;
		}
	}

	void ScrollBarControl::onThumbTrack(POINT pos)
	{
		const auto minPos = m_leftArrow.right + ARROW_SIZE / 2;
		const auto maxPos = m_rightArrow.left - ARROW_SIZE / 2;
		pos.x = std::max(pos.x, minPos);
		pos.x = std::min(pos.x, maxPos);
		setPos(m_min + roundDiv((pos.x - minPos) * (m_max - m_min), maxPos - minPos));
	}

	void CALLBACK ScrollBarControl::repeatTimerProc(HWND /*hwnd*/, UINT /*message*/, UINT_PTR /*iTimerID*/, DWORD /*dwTime*/)
	{
		static_cast<ScrollBarControl*>(Input::getCapture())->onRepeat();
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
