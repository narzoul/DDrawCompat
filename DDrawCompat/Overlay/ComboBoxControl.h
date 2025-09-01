#pragma once

#include <string>
#include <vector>

#include <Overlay/ComboBoxDropDown.h>
#include <Overlay/Control.h>
#include <Overlay/LabelControl.h>

namespace Overlay
{
	class ComboBoxControl : public Control
	{
	public:
		ComboBoxControl(Control& parent, const RECT& rect, const std::vector<std::string>& values);

		virtual void setRect(const RECT& rect) override;

		const std::string& getValue() const { return m_label.getLabel(); }
		void setValue(const std::string& value);

	private:
		virtual void draw(HDC dc) override;
		virtual void onLButtonDown(POINT pos) override;

		LabelControl m_label;
		ComboBoxDropDown m_dropDown;
	};
}
