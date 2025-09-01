#pragma once

#include <functional>
#include <memory>

#include <Overlay/LabelControl.h>
#include <Overlay/ScrollBarControl.h>
#include <Overlay/ShaderStatusControl.h>

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
		static const int SETTING_LABEL_WIDTH = 140;
		static const int SETTING_CONTROL_WIDTH = 150;
		static const int SHADER_SETTING_CONTROL_WIDTH = 380;
		static const int TOTAL_WIDTH =
			SETTING_LABEL_WIDTH + SETTING_CONTROL_WIDTH + PARAM_LABEL_WIDTH + PARAM_CONTROL_WIDTH + BORDER;

		SettingControl(ConfigWindow& parent, const RECT& rect, Config::Setting& setting,
			UpdateFunc updateFunc, bool isReadOnly);

		virtual RECT getHighlightRect() const override;
		virtual void onLButtonDown(POINT pos) override;
		virtual void onMouseWheel(POINT pos, SHORT delta) override;
		virtual void onNotify(Control& control) override;

		Config::Setting& getSetting() const { return m_setting; }
		Control* getValueControl() const { return m_valueControl.get(); }
		void reset();

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
		std::unique_ptr<ShaderStatusControl> m_shaderStatusControl;
	};
}
