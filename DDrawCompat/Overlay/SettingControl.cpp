#include <Config/Config.h>
#include <Config/Parser.h>
#include <Config/Setting.h>
#include <D3dDdi/Device.h>
#include <DDraw/RealPrimarySurface.h>
#include <Overlay/ComboBoxControl.h>
#include <Overlay/SettingControl.h>

namespace Overlay
{
	SettingControl::SettingControl(Control& parent, const RECT& rect, Config::Setting& setting)
		: Control(&parent, rect, WS_VISIBLE)
		, m_setting(setting)
		, m_settingLabel(*this, { rect.left, rect.top, rect.left + SETTING_LABEL_WIDTH, rect.bottom }, setting.getName() + ':', 0)
	{
		const RECT r = { rect.left + SETTING_LABEL_WIDTH, rect.top + BORDER / 2,
			rect.left + SETTING_LABEL_WIDTH + SETTING_CONTROL_WIDTH, rect.bottom - BORDER / 2 };
		m_valueControl.reset(new ComboBoxControl(*this, r));
		getValueComboBox().setValue(setting.getValueStr());
		getValueComboBox().setValues(setting.getDefaultValueStrings());
		onValueChanged();
		updateValuesParam();
	}

	ComboBoxControl& SettingControl::getValueComboBox() const
	{
		return static_cast<ComboBoxControl&>(*m_valueControl);
	}

	void SettingControl::onNotify(Control& control)
	{
		if (&control == m_paramControl.get())
		{
			onParamChanged();
		}
		else
		{
			onValueChanged();
		}

		if (&Config::antialiasing  == &m_setting ||
			&Config::textureFilter == &m_setting)
		{
			D3dDdi::Device::updateAllConfig();
		}

		invalidate(m_rect);
	}

	void SettingControl::onParamChanged()
	{
		const std::string value(Config::Parser::removeParam(m_setting.getValueStr()) +
			'(' + std::to_string(m_paramControl->getPos()) + ')');
		m_setting.set(value);
		getValueComboBox().setValue(value);
		updateValuesParam();
	}

	void SettingControl::onValueChanged()
	{
		const std::string value(getValueComboBox().getValue());
		m_setting.set(value);

		if (m_paramControl)
		{
			m_paramLabel.reset();
			m_paramControl.reset();
		}

		const auto paramInfo = m_setting.getParamInfo();
		if (!paramInfo.name.empty())
		{
			RECT r = m_valueControl->getRect();
			r.left = r.right;
			r.right = r.left + PARAM_LABEL_WIDTH;
			m_paramLabel.reset(new LabelControl(*this, r, paramInfo.name + ':', 0));

			r.left = r.right;
			r.right = r.left + PARAM_CONTROL_WIDTH;
			m_paramControl.reset(new ScrollBarControl(*this, r, paramInfo.min, paramInfo.max));
			m_paramControl->setPos(m_setting.getParam());
		}
	}

	void SettingControl::updateValuesParam()
	{
		const auto currentValue(Config::Parser::removeParam(m_setting.getValueStr()));
		auto values(getValueComboBox().getValues());
		for (auto& v : values)
		{
			if (Config::Parser::removeParam(v) == currentValue)
			{
				v = m_setting.getValueStr();
				getValueComboBox().setValues(values);
				break;
			}
		}
	}
}
