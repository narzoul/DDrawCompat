#pragma once

#include <Overlay/Control.h>

namespace Overlay
{
	class ScrollBarControl : public Control
	{
	public:
		ScrollBarControl(Control& parent, const RECT& rect, int min, int max, int pos, DWORD style = WS_VISIBLE);

		virtual void onLButtonDown(POINT pos) override;
		virtual void onLButtonUp(POINT pos) override;
		virtual void onMouseMove(POINT pos) override;
		virtual void onMouseWheel(POINT pos, SHORT delta) override;

		int getPos() const { return m_pos; }
		void setPos(int pos);
		void setPageSize(int pageSize) { m_pageSize = pageSize; }

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

		RECT getLeftArrowRect() const;
		RECT getRightArrowRect() const;
		int getPageSize() const;
		RECT getThumbRect() const;
		bool isHorizontal() const;
		void onRepeat();
		void scroll();

		static void CALLBACK repeatTimerProc(HWND hwnd, UINT message, UINT_PTR iTimerID, DWORD dwTime);
		static void startRepeatTimer(DWORD time);
		static void stopRepeatTimer();

		int m_min;
		int m_max;
		int m_pos;
		int m_pageSize;
		State m_state;
		LONG RECT::* m_left;
		LONG RECT::* m_top;
		LONG RECT::* m_right;
		LONG RECT::* m_bottom;
		LONG POINT::* m_x;
	};
}
