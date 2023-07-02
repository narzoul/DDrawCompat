#pragma once

#include <Overlay/Control.h>

namespace Overlay
{
	class ScrollBarControl : public Control
	{
	public:
		ScrollBarControl(Control& parent, const RECT& rect, int min, int max);

		int getPos() const { return m_pos; }
		void setPos(int pos);

	private:
		enum class State
		{
			IDLE,
			LEFT_ARROW_PRESSED,
			RIGHT_ARROW_PRESSED,
			LEFT_SHAFT_PRESSED,
			RIGHT_SHAFT_PRESSED,
			THUMB_PRESSED
		};

		virtual void draw(HDC dc) override;
		virtual void onLButtonDown(POINT pos) override;
		virtual void onLButtonUp(POINT pos) override;
		virtual void onMouseMove(POINT pos) override;

		int getPageSize() const;
		RECT getThumbRect() const;
		void onRepeat();
		void scroll();

		static void CALLBACK repeatTimerProc(HWND hwnd, UINT message, UINT_PTR iTimerID, DWORD dwTime);
		static void startRepeatTimer(DWORD time);
		static void stopRepeatTimer();

		int m_min;
		int m_max;
		int m_pos;
		RECT m_leftArrow;
		RECT m_rightArrow;
		State m_state;
	};
}
