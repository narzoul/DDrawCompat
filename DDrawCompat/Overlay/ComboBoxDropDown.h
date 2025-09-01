#pragma once

#include <list>
#include <memory>
#include <vector>

#include <Overlay/LabelControl.h>
#include <Overlay/ScrollBarControl.h>
#include <Overlay/Window.h>

namespace Overlay
{
	class ComboBoxControl;

	class ComboBoxDropDown : public Window
	{
	public:
		ComboBoxDropDown(ComboBoxControl& parent, const std::vector<std::string>& values);

		virtual void onLButtonDown(POINT pos) override;
		virtual void onMouseWheel(POINT pos, SHORT delta) override;
		virtual void onNotify(Control& control) override;
		virtual void setVisible(bool visible) override;

		void select(const std::string& value);

	private:
		virtual RECT calculateRect(const RECT& monitorRect) const override;

		void addItems();
		LONG findValue(const std::string& value) const;
		void initTopRowIndex();
		LONG getMaxItemWidth() const;

		ComboBoxControl& m_parent;
		std::unique_ptr<ScrollBarControl> m_scrollBar;
		const std::vector<std::string> m_values;
		std::list<LabelControl> m_labels;
		LONG m_rowsPerPage;
		LONG m_topRowIndex;
	};
}
