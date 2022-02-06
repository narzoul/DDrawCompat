#include <Common/Hook.h>
#include <Overlay/ComboBoxControl.h>
#include <Overlay/ComboBoxDropDown.h>

namespace Overlay
{
	ComboBoxDropDown::ComboBoxDropDown(ComboBoxControl& parent, const std::vector<std::string>& values)
		: Window(&static_cast<Window&>(parent.getRoot()), calculateRect(parent, values.size()))
		, m_parent(parent)
	{
		setValues(values);
	}

	RECT ComboBoxDropDown::calculateRect(ComboBoxControl& parent, DWORD itemCount)
	{
		const RECT parentRect = parent.getRect();
		return { parentRect.left, parentRect.bottom, parentRect.right,
			parentRect.bottom + static_cast<int>(itemCount) * ARROW_SIZE };
	}

	RECT ComboBoxDropDown::calculateRect(const RECT& /*monitorRect*/) const
	{
		const Window& rootWindow = static_cast<const Window&>(m_parent.getRoot());
		const RECT rootRect = rootWindow.getRect();
		RECT r = calculateRect(m_parent, m_values.size());
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
