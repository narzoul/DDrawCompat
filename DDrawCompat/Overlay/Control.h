#pragma once

#include <set>

#include <Windows.h>

namespace Overlay
{
	class Control
	{
	public:
		static const int ARROW_SIZE = 17;
		static const int BORDER = 8;

		Control(Control* parent, const RECT& rect, DWORD style);
		virtual ~Control();

		Control(const Control&) = delete;
		Control(Control&&) = delete;
		Control& operator=(const Control&) = delete;
		Control& operator=(Control&&) = delete;

		virtual void draw(HDC /*dc*/) {}
		virtual void invalidate();
		virtual void onLButtonDown(POINT pos);
		virtual void onLButtonUp(POINT pos);
		virtual void onMouseMove(POINT pos);
		virtual void onNotify(Control& /*control*/) {}
		virtual void setVisible(bool isVisible);

		void drawAll(HDC dc);
		Control* getParent() const { return m_parent; }
		RECT getRect() const { return m_rect; }
		const Control& getRoot() const;
		Control& getRoot();
		bool isVisible() const { return m_style & WS_VISIBLE; }

	protected:
		void drawArrow(HDC dc, RECT rect, UINT state);
		void propagateMouseEvent(void(Control::* onEvent)(POINT), POINT pos);

		static Control* getCapture();
		static void setCapture(Control* control);

		Control* m_parent;
		RECT m_rect;
		DWORD m_style;
		std::set<Control*> m_children;
	};
}
