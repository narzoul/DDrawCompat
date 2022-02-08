#pragma once

#include <list>
#include <vector>

#include <Overlay/LabelControl.h>
#include <Overlay/Window.h>

namespace Overlay
{
	class ComboBoxControl;

	class ComboBoxDropDown : public Window
	{
	public:
		ComboBoxDropDown(ComboBoxControl& parent, const std::vector<std::string>& values);

		virtual void onLButtonDown(POINT pos) override;
		virtual void onNotify(Control& control) override;
		virtual void setVisible(bool visible) override;

		void select(const std::string& value);

	private:
		static RECT calculateRect(ComboBoxControl& parent, DWORD itemCount);

		virtual RECT calculateRect(const RECT& monitorRect) const override;

		ComboBoxControl& m_parent;
		std::list<LabelControl> m_labels;
	};
}
