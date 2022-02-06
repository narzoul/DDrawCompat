#include <Common/Hook.h>
#include <Overlay/Control.h>

namespace Overlay
{
	Control* g_capture = nullptr;

	Control::Control(Control* parent, const RECT& rect, DWORD style)
		: m_parent(parent)
		, m_rect(rect)
		, m_style(style)
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

	Control* Control::getCapture()
	{
		return g_capture;
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
		propagateMouseEvent(&Control::onMouseMove, pos);
	}

	void Control::propagateMouseEvent(void(Control::* onEvent)(POINT), POINT pos)
	{
		if (g_capture)
		{
			(g_capture->*onEvent)(pos);
			return;
		}

		for (auto child : m_children)
		{
			if (PtInRect(&child->m_rect, pos))
			{
				(child->*onEvent)(pos);
				return;
			}
		}
	}

	void Control::setCapture(Control* control)
	{
		g_capture = control;
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
