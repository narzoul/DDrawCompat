#include <Common/Hook.h>
#include <Common/Log.h>
#include <Config/Parser.h>
#include <Config/Settings/ConfigTransparency.h>
#include <Input/Input.h>
#include <Overlay/ComboBoxControl.h>
#include <Overlay/ComboBoxDropDown.h>

namespace
{
	const LONG ROW_HEIGHT = Overlay::Control::ARROW_SIZE;
}

namespace Overlay
{
	ComboBoxDropDown::ComboBoxDropDown(ComboBoxControl& parent, const std::vector<std::string>& values)
		: Window(&static_cast<Window&>(parent.getRoot()), {}, WS_BORDER,
			Config::configTransparency.get())
		, m_parent(parent)
		, m_values(values)
		, m_rowsPerPage(0)
		, m_topRowIndex(-1)
	{
	}

	void ComboBoxDropDown::addItems()
	{
		m_labels.clear();
		RECT r = { 2, 2, m_rect.right - m_rect.left - 2 - (m_scrollBar ? ARROW_SIZE - 1 : 0), ROW_HEIGHT + 2};
		for (LONG i = 0; i < m_rowsPerPage; ++i)
		{
			m_labels.emplace_back(*this, r, m_values[m_topRowIndex + i], TA_LEFT, WS_VISIBLE | WS_CLIPCHILDREN | WS_TABSTOP);
			OffsetRect(&r, 0, ROW_HEIGHT);
		}
		select(m_parent.getValue());
	}

	RECT ComboBoxDropDown::calculateRect(const RECT& monitorRect) const
	{
		const RECT rootWindowRect = static_cast<const Window&>(m_parent.getRoot()).getRect();
		RECT parentRect = m_parent.getRect();
		OffsetRect(&parentRect, rootWindowRect.left, rootWindowRect.top);

		const LONG maxRowsAbove = (parentRect.top - monitorRect.top - 4) / ROW_HEIGHT;
		const LONG maxRowsBelow = (monitorRect.bottom - parentRect.bottom - 4) / ROW_HEIGHT;

		RECT rect = { parentRect.left, 0, parentRect.right, 0 };
		if (static_cast<LONG>(m_values.size()) > maxRowsBelow && maxRowsAbove > maxRowsBelow)
		{
			rect.bottom = parentRect.top;
			rect.top = parentRect.top - std::min<LONG>(maxRowsAbove, m_values.size()) * ROW_HEIGHT - 4;
		}
		else
		{
			rect.top = parentRect.bottom;
			rect.bottom = parentRect.bottom + std::min<LONG>(maxRowsBelow, m_values.size()) * ROW_HEIGHT + 4;
		}

		const UINT rowsPerPage = (m_rect.bottom - m_rect.top - 4) / ROW_HEIGHT;
		const LONG maxWidth = getMaxItemWidth() + (rowsPerPage < m_values.size() ? ARROW_SIZE : 0);
		if (maxWidth > rect.right - rect.left)
		{
			if (maxWidth > monitorRect.right - rect.left)
			{
				rect.right = monitorRect.right;
				rect.left = std::max(rect.right - maxWidth, 0L);
			}
			else
			{
				rect.right = std::min(rect.left + maxWidth, monitorRect.right);
			}
		}
		return rect;
	}

	LONG ComboBoxDropDown::getMaxItemWidth() const
	{
		LONG maxWidth = 0;
		HDC dc = CreateCompatibleDC(nullptr);
		auto origFont = SelectObject(dc, getFont());

		for (const auto& value : m_values)
		{
			SIZE size = {};
			GetTextExtentPoint32(dc, value.c_str(), value.length(), &size);
			if (size.cx > maxWidth)
			{
				maxWidth = size.cx;
			}
		}

		SelectObject(dc, origFont);
		DeleteDC(dc);
		return maxWidth + 2 * BORDER + 4;
	}

	LONG ComboBoxDropDown::findValue(const std::string& value) const
	{
		const auto valueWithoutParam(Config::Parser::removeParam(value));
		for (std::size_t i = 0; i < m_values.size(); ++i)
		{
			if (Config::Parser::removeParam(m_values[i]) == valueWithoutParam)
			{
				return i;
			}
		}
		return -1;
	}

	void ComboBoxDropDown::initTopRowIndex()
	{
		const auto index = findValue(m_parent.getValue());
		if (index >= m_rowsPerPage)
		{
			m_topRowIndex = index - m_rowsPerPage / 2;
		}
		if (m_topRowIndex < 0)
		{
			m_topRowIndex = 0;
		}
		else
		{
			const LONG maxTopRowIndex = m_values.size() - m_rowsPerPage;
			if (m_topRowIndex > maxTopRowIndex)
			{
				m_topRowIndex = maxTopRowIndex;
			}
		}
	}

	void ComboBoxDropDown::onLButtonDown(POINT pos)
	{
		if (PtInRect(&m_rect, { m_rect.left + pos.x, m_rect.top + pos.y }))
		{
			Control::onLButtonDown(pos);
		}
		else
		{
			setVisible(false);
			Input::setCapture(m_parent.getParent());
		}
	}

	void ComboBoxDropDown::onMouseWheel(POINT pos, SHORT delta)
	{
		if (m_scrollBar)
		{
			m_scrollBar->onMouseWheel(pos, delta);
			onMouseMove(pos);
		}
	}

	void ComboBoxDropDown::onNotify(Control& control)
	{
		if (&control == m_scrollBar.get())
		{
			m_topRowIndex = m_scrollBar->getPos();
			addItems();
		}
		else
		{
			m_parent.setValue(static_cast<LabelControl&>(control).getLabel());
			m_parent.getParent()->onNotify(m_parent);
		}
	}

	void ComboBoxDropDown::select(const std::string& value)
	{
		for (auto& label : m_labels)
		{
			if (HIGHLIGHT_COLOR == label.getColor())
			{
				label.setColor(FOREGROUND_COLOR);
				break;
			}
		}

		const auto index = findValue(value);
		if (index >= m_topRowIndex && index < m_topRowIndex + m_rowsPerPage)
		{
			auto it = m_labels.begin();
			std::advance(it, index - m_topRowIndex);
			it->setLabel(value);
			it->setColor(HIGHLIGHT_COLOR);
		}

		invalidate();
	}

	void ComboBoxDropDown::setVisible(bool visible)
	{
		m_highlightedChild = nullptr;
		Window::setVisible(visible);
		Input::setCapture(visible ? this : m_parentWindow);

		if (!visible)
		{
			return;
		}

		m_rowsPerPage = (m_rect.bottom - m_rect.top - 4) / ROW_HEIGHT;
		if (-1 == m_topRowIndex)
		{
			initTopRowIndex();
		}

		if (m_rowsPerPage < static_cast<LONG>(m_values.size()))
		{
			RECT r = { 0, 0, m_rect.right - m_rect.left, m_rect.bottom - m_rect.top };
			r.left = r.right - ARROW_SIZE;
			m_scrollBar.reset(new ScrollBarControl(*this, r, 0, m_values.size() - m_rowsPerPage, 0, WS_VISIBLE));
			m_scrollBar->setPos(m_topRowIndex);
			m_scrollBar->setPageSize(3);
		}
		else
		{
			m_scrollBar.reset();
		}

		addItems();
	}
}
