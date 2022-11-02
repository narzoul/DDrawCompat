#include <Common/Hook.h>
#include <Common/Log.h>
#include <Config/Parser.h>
#include <Input/Input.h>
#include <Overlay/ComboBoxControl.h>
#include <Overlay/ComboBoxDropDown.h>

namespace Overlay
{
	ComboBoxDropDown::ComboBoxDropDown(ComboBoxControl& parent, const std::vector<std::string>& values)
		: Window(&static_cast<Window&>(parent.getRoot()), calculateRect(parent, values.size()), WS_BORDER)
		, m_parent(parent)
	{
		for (int i = 0; i < static_cast<int>(values.size()); ++i)
		{
			m_labels.emplace_back(*this,
				RECT{ 2, i * ARROW_SIZE + 2, m_rect.right - m_rect.left - 2, (i + 1) * ARROW_SIZE + 2 },
				values[i], DT_SINGLELINE | DT_VCENTER, WS_VISIBLE | WS_TABSTOP);
		}
	}

	RECT ComboBoxDropDown::calculateRect(ComboBoxControl& parent, DWORD itemCount)
	{
		const RECT parentRect = parent.getRect();
		RECT rect = { parentRect.left, parentRect.bottom, parentRect.right,
			parentRect.bottom + static_cast<int>(itemCount) * ARROW_SIZE + 4 };

		const RECT rootWindowRect = static_cast<const Window&>(parent.getRoot()).getRect();
		if (rect.bottom > rootWindowRect.bottom - rootWindowRect.top)
		{
			OffsetRect(&rect, 0, parentRect.top - rect.bottom);
		}
		return rect;
	}

	RECT ComboBoxDropDown::calculateRect(const RECT& /*monitorRect*/) const
	{
		const Window& rootWindow = static_cast<const Window&>(m_parent.getRoot());
		const RECT rootRect = rootWindow.getRect();
		RECT r = calculateRect(m_parent, m_labels.size());
		OffsetRect(&r, rootRect.left, rootRect.top);
		return r;
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

	void ComboBoxDropDown::onNotify(Control& control)
	{
		m_parent.setValue(static_cast<LabelControl&>(control).getLabel());
		m_parent.getParent()->onNotify(m_parent);
	}

	void ComboBoxDropDown::select(const std::string& value)
	{
		const auto valueWithoutParam(Config::Parser::removeParam(value));
		for (auto& label : m_labels)
		{
			if (Config::Parser::removeParam(label.getLabel()) == valueWithoutParam)
			{
				label.setLabel(value);
				label.setColor(HIGHLIGHT_COLOR);
			}
			else
			{
				label.setColor(FOREGROUND_COLOR);
			}
		}
		invalidate();
	}

	void ComboBoxDropDown::setVisible(bool visible)
	{
		m_highlightedChild = nullptr;
		Window::setVisible(visible);
		Input::setCapture(visible ? this : m_parentWindow);
	}
}
