#pragma once

#include <memory>
#include <string>

#include <Overlay/LabelControl.h>
#include <Overlay/ScrollBarControl.h>

namespace Config
{
	class Setting;
}

namespace Overlay
{
	class ComboBoxControl;

	class SettingControl : public Control
	{
	public:
		static const int PARAM_LABEL_WIDTH = 50;
		static const int PARAM_CONTROL_WIDTH = 151;
		static const int SETTING_LABEL_WIDTH = 120;
		static const int SETTING_CONTROL_WIDTH = 151;
		static const int TOTAL_WIDTH =
			SETTING_LABEL_WIDTH + SETTING_CONTROL_WIDTH + PARAM_LABEL_WIDTH + PARAM_CONTROL_WIDTH + BORDER;

		SettingControl(Control& parent, const RECT& rect, Config::Setting& setting);

		virtual void onNotify(Control& control) override;

	private:
		ComboBoxControl& getValueComboBox() const;
		void onParamChanged();
		void onValueChanged();
		void updateValuesParam();

		Config::Setting& m_setting;
		LabelControl m_settingLabel;
		std::unique_ptr<Control> m_valueControl;
		std::unique_ptr<LabelControl> m_paramLabel;
		std::unique_ptr<ScrollBarControl> m_paramControl;
	};
}
