#include <Config/Settings/Antialiasing.h>
#include <Config/Settings/ColorKeyMethod.h>
#include <Config/Settings/DepthFormat.h>
#include <Config/Settings/RenderColorDepth.h>
#include <Config/Settings/ResolutionScale.h>
#include <Config/Settings/SpriteFilter.h>
#include <Config/Settings/SpriteTexCoord.h>
#include <Config/Settings/TextureFilter.h>
#include <Config/Setting.h>
#include <Overlay/ComboBoxControl.h>
#include <Overlay/ConfigWindow.h>
#include <Overlay/SettingControl.h>

namespace
{
	std::vector<std::string> getValueStrings(Config::Setting& setting)
	{
		auto values(setting.getDefaultValueStrings());
		const auto currentValue = setting.getValueStr();
		for (const auto& value : values)
		{
			if (Config::Parser::removeParam(value) == Config::Parser::removeParam(currentValue))
			{
				return values;
			}
		}
		values.push_back(currentValue);
		return values;
	}
}

namespace Overlay
{
	SettingControl::SettingControl(ConfigWindow& parent, const RECT& rect, Config::Setting& setting, UpdateFunc updateFunc)
		: Control(&parent, rect, WS_VISIBLE | WS_TABSTOP)
		, m_setting(setting)
		, m_updateFunc(updateFunc)
		, m_settingLabel(*this, { rect.left, rect.top, rect.left + SETTING_LABEL_WIDTH, rect.bottom }, setting.getName() + ':', 0)
	{
		const RECT r = { rect.left + SETTING_LABEL_WIDTH, rect.top + BORDER / 2,
			rect.left + SETTING_LABEL_WIDTH + SETTING_CONTROL_WIDTH, rect.bottom - BORDER / 2 };
		m_valueControl.reset(new ComboBoxControl(*this, r, getValueStrings(setting)));
		getValueComboBox().setValue(setting.getValueStr());
		onValueChanged();
	}

	RECT SettingControl::getHighlightRect() const
	{
		RECT r = m_rect;
		InflateRect(&r, -BORDER / 2, 0);
		return r;
	}

	ComboBoxControl& SettingControl::getValueComboBox() const
	{
		return static_cast<ComboBoxControl&>(*m_valueControl);
	}

	void SettingControl::onLButtonDown(POINT pos)
	{
		auto configWindow = static_cast<ConfigWindow*>(m_parent);
		if (PtInRect(&m_rect, pos))
		{
			configWindow->setFocus(this);
			Control::onLButtonDown(pos);
		}
		else
		{
			configWindow->updateButtons();
			configWindow->setFocus(nullptr);
			configWindow->onMouseMove(pos);
		}
	}

	void SettingControl::onMouseWheel(POINT pos, SHORT delta)
	{
		if (m_paramControl)
		{
			m_paramControl->onMouseWheel(pos, delta);
		}
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

		if (m_updateFunc)
		{
			m_updateFunc();
		}

		invalidate();
	}

	void SettingControl::onParamChanged()
	{
		const std::string value(Config::Parser::removeParam(m_setting.getValueStr()) +
			'(' + std::to_string(m_paramControl->getPos()) + ')');
		m_setting.set(value);
		getValueComboBox().setValue(value);
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

	void SettingControl::set(const std::string& value)
	{
		if (m_setting.getValueStr() != value)
		{
			getValueComboBox().setValue(value);
			onNotify(*m_valueControl);
		}
	}
}
