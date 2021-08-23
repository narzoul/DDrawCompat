#include <Common/Hook.h>
#include <Overlay/ComboBoxControl.h>
#include <Overlay/ComboBoxDropDown.h>

namespace Overlay
{
	ComboBoxDropDown::ComboBoxDropDown(ComboBoxControl& parent)
		: Window(&static_cast<Window&>(parent.getRoot()), { 0, 0, 100, 100 })
		, m_parent(parent)
	{
	}

	RECT ComboBoxDropDown::calculateRect(const RECT& monitorRect) const
	{
		const RECT parentRect = m_parent.getRect();
		RECT r = { parentRect.left, parentRect.bottom,
			parentRect.right, parentRect.bottom + static_cast<int>(m_parent.getValues().size()) * ARROW_SIZE };

		const Window& rootWindow = static_cast<const Window&>(m_parent.getRoot());
		const RECT rootRect = rootWindow.calculateRect(monitorRect);
		OffsetRect(&r, rootRect.left, rootRect.top);
		return r;
	}

	void ComboBoxDropDown::onLButtonDown(POINT pos)
	{
		if (PtInRect(&m_rect, { m_rect.left + pos.x, m_rect.top + pos.y }))
		{
			propagateMouseEvent(&Control::onLButtonDown, pos);
		}
		else
		{
			setVisible(false);
		}
	}

	void ComboBoxDropDown::onNotify(Control& control)
	{
		m_parent.setValue(static_cast<LabelControl&>(control).getLabel());
		m_parent.getParent()->onNotify(*m_parent.getParent());
	}

	void ComboBoxDropDown::setValues(const std::vector<std::string>& values)
	{
		m_values = values;
		m_labels.clear();
		int i = 0;
		for (const auto& v : values)
		{
			m_labels.emplace_back(*this,
				RECT{ BORDER, i * ARROW_SIZE, m_rect.right - m_rect.left - BORDER, (i + 1) * ARROW_SIZE },
				v, DT_SINGLELINE | DT_VCENTER);
			++i;
		}
	}
}
