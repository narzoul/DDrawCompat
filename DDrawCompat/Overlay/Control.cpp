#include <Common/Hook.h>
#include <Overlay/Control.h>

namespace Overlay
{
	Control::Control(Control* parent, const RECT& rect, DWORD style)
		: m_parent(parent)
		, m_rect(rect)
		, m_style(style)
		, m_highlightedChild(nullptr)
	{
		if (parent)
		{
			parent->m_children.insert(this);
		}
	}

	Control::~Control()
	{
		if (m_parent)
		{
			m_parent->m_children.erase(this);
		}
	}

	void Control::drawAll(HDC dc)
	{
		draw(dc);

		for (auto control : m_children)
		{
			control->drawAll(dc);
		}

		if (m_style & WS_BORDER)
		{
			RECT r = m_rect;
			if (!m_parent)
			{
				OffsetRect(&r, -r.left, -r.top);
			}
			CALL_ORIG_FUNC(Rectangle)(dc, r.left, r.top, r.right, r.bottom);
		}

		if (m_highlightedChild)
		{
			RECT r = m_highlightedChild->getHighlightRect();
			SetDCPenColor(dc, HIGHLIGHT_COLOR);
			CALL_ORIG_FUNC(Rectangle)(dc, r.left, r.top, r.right, r.bottom);
			SetDCPenColor(dc, FOREGROUND_COLOR);
		}
	}

	void Control::drawArrow(HDC dc, RECT rect, UINT state)
	{
		CALL_ORIG_FUNC(Rectangle)(dc, rect.left, rect.top, rect.right, rect.bottom);

		POINT center = { (rect.left + rect.right) / 2, (rect.top + rect.bottom) / 2 };
		rect = { center.x, center.y, center.x, center.y };
		InflateRect(&rect, 3, 3);
		POINT poly[3] = {};

		switch (state)
		{
		case DFCS_SCROLLDOWN:
			poly[0] = { rect.left, rect.top };
			poly[1] = { rect.right, rect.top };
			poly[2] = { center.x, rect.bottom };
			break;

		case DFCS_SCROLLLEFT:
			poly[0] = { rect.left, center.y };
			poly[1] = { rect.right, rect.top };
			poly[2] = { rect.right, rect.bottom };
			break;

		case DFCS_SCROLLRIGHT:
			poly[0] = { rect.left, rect.top };
			poly[1] = { rect.left, rect.bottom };
			poly[2] = { rect.right, center.y };
			break;

		case DFCS_SCROLLUP:
			poly[0] = { center.x, rect.top };
			poly[1] = { rect.left, rect.bottom };
			poly[2] = { rect.right, rect.bottom };
			break;
		}

		CALL_ORIG_FUNC(Polygon)(dc, poly, 3);
	}

	const Control& Control::getRoot() const
	{
		if (m_parent)
		{
			return m_parent->getRoot();
		}
		return *this;
	}

	Control& Control::getRoot()
	{
		return const_cast<Control&>(std::as_const(*this).getRoot());
	}

	void Control::invalidate()
	{
		if (m_parent)
		{
			m_parent->invalidate();
		}
	}

	void Control::onLButtonDown(POINT pos)
	{
		propagateMouseEvent(&Control::onLButtonDown, pos);
	}

	void Control::onLButtonUp(POINT pos)
	{
		propagateMouseEvent(&Control::onLButtonUp, pos);
	}

	void Control::onMouseMove(POINT pos)
	{
		auto prevHighlightedChild = m_highlightedChild;
		m_highlightedChild = nullptr;
		propagateMouseEvent(&Control::onMouseMove, pos);
		if (m_highlightedChild != prevHighlightedChild)
		{
			invalidate();
		}
		if (m_parent && (m_style & WS_TABSTOP))
		{
			m_parent->m_highlightedChild = this;
			invalidate();
		}
	}

	void Control::propagateMouseEvent(void(Control::* onEvent)(POINT), POINT pos)
	{
		for (auto child : m_children)
		{
			if (PtInRect(&child->m_rect, pos))
			{
				(child->*onEvent)(pos);
				return;
			}
		}
	}

	void Control::setVisible(bool isVisible)
	{
		if (isVisible != Control::isVisible())
		{
			m_style ^= WS_VISIBLE;
			invalidate();
		}
	}
}
