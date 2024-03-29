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
		virtual RECT getHighlightRect() const { return m_rect; }
		virtual void invalidate();
		virtual void onLButtonDown(POINT pos);
		virtual void onLButtonUp(POINT pos);
		virtual void onMouseMove(POINT pos);
		virtual void onMouseWheel(POINT pos, SHORT delta);
		virtual void onNotify(Control& /*control*/) {}
		virtual void setVisible(bool isVisible);

		void drawAll(HDC dc);
		Control* getParent() const { return m_parent; }
		RECT getRect() const { return m_rect; }
		const Control& getRoot() const;
		Control& getRoot();
		DWORD getStyle() const { return m_style; }
		bool isEnabled() const;
		bool isVisible() const { return m_style & WS_VISIBLE; }
		void setEnabled(bool isEnabled);

	protected:
		static const COLORREF DISABLED_COLOR = RGB(192, 192, 192);
		static const COLORREF FOREGROUND_COLOR = RGB(0, 255, 0);
		static const COLORREF HIGHLIGHT_COLOR = RGB(255, 255, 0);

		void drawArrow(HDC dc, RECT rect, UINT state);

		template <typename... Params>
		void propagateMouseEvent(void(Control::* onEvent)(POINT, Params...), POINT pos, Params... params);

		Control* m_parent;
		RECT m_rect;
		DWORD m_style;
		std::set<Control*> m_children;
		Control* m_highlightedChild;
	};
}
