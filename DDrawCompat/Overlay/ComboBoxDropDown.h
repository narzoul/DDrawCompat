#pragma once

#include <list>
#include <vector>

#include <Overlay/LabelControl.h>
#include <Overlay/Window.h>

namespace Overlay
{
	class ComboBoxControl;
	class LabelControl;

	class ComboBoxDropDown : public Window
	{
	public:
		ComboBoxDropDown(ComboBoxControl& parent, const std::vector<std::string>& values);

		virtual void onNotify(Control& control) override;

		std::vector<std::string> getValues() const { return m_values; }
		void setValues(const std::vector<std::string>& values);

	private:
		static RECT calculateRect(ComboBoxControl& parent, DWORD itemCount);

		virtual RECT calculateRect(const RECT& monitorRect) const override;
		virtual void onLButtonDown(POINT pos) override;

		ComboBoxControl& m_parent;
		std::vector<std::string> m_values;
		std::list<LabelControl> m_labels;
	};
}
