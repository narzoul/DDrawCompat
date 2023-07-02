#pragma once

#include <functional>
#include <memory>

#include <Overlay/LabelControl.h>
#include <Overlay/ScrollBarControl.h>

namespace Config
{
	class Setting;
}

namespace Overlay
{
	class ComboBoxControl;
	class ConfigWindow;

	class SettingControl : public Control
	{
	public:
		typedef std::function<void()> UpdateFunc;

		static const int PARAM_LABEL_WIDTH = 70;
		static const int PARAM_CONTROL_WIDTH = 241;
		static const int SETTING_LABEL_WIDTH = 130;
		static const int SETTING_CONTROL_WIDTH = 141;
		static const int TOTAL_WIDTH =
			SETTING_LABEL_WIDTH + SETTING_CONTROL_WIDTH + PARAM_LABEL_WIDTH + PARAM_CONTROL_WIDTH + BORDER;

		SettingControl(ConfigWindow& parent, const RECT& rect, Config::Setting& setting, UpdateFunc updateFunc);

		virtual RECT getHighlightRect() const override;
		virtual void onLButtonDown(POINT pos) override;
		virtual void onMouseWheel(POINT pos, SHORT delta) override;
		virtual void onNotify(Control& control) override;

		Config::Setting& getSetting() const { return m_setting; }
		void set(const std::string& value);

	private:
		ComboBoxControl& getValueComboBox() const;
		void onParamChanged();
		void onValueChanged();

		Config::Setting& m_setting;
		UpdateFunc m_updateFunc;
		LabelControl m_settingLabel;
		std::unique_ptr<Control> m_valueControl;
		std::unique_ptr<LabelControl> m_paramLabel;
		std::unique_ptr<ScrollBarControl> m_paramControl;
	};
}
