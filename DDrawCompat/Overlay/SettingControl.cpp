#include <Config/Parser.h>
#include <Config/Setting.h>
#include <D3dDdi/ScopedCriticalSection.h>
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
	SettingControl::SettingControl(ConfigWindow& parent, const RECT& rect, Config::Setting& setting,
		UpdateFunc updateFunc, bool isReadOnly)
		: Control(&parent, rect, WS_VISIBLE | WS_TABSTOP | (isReadOnly ? WS_DISABLED : 0))
		, m_setting(setting)
		, m_updateFunc(updateFunc)
		, m_settingLabel(*this, { rect.left, rect.top, rect.left + SETTING_LABEL_WIDTH, rect.bottom }, setting.getName() + ':', 0)
	{
		RECT r = { rect.left + SETTING_LABEL_WIDTH, rect.top + BORDER / 2,
			rect.left + SETTING_LABEL_WIDTH + SETTING_CONTROL_WIDTH, rect.bottom - BORDER / 2 };

		if (isReadOnly)
		{
			r.right += PARAM_LABEL_WIDTH + PARAM_CONTROL_WIDTH;
			m_valueControl.reset(new LabelControl(*this, r, setting.getValueStr(), 0));
		}
		else
		{
			m_valueControl.reset(new ComboBoxControl(*this, r, getValueStrings(setting)));
			getValueComboBox().setValue(setting.getValueStr());
			onValueChanged();
		}
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

	void SettingControl::invalidateShaderStatus()
	{
		if (m_shaderStatusControl)
		{
			m_shaderStatusControl->invalidate();
		}
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
		D3dDdi::ScopedCriticalSection lock;
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
		D3dDdi::ScopedCriticalSection lock;
		const std::string value(Config::Parser::removeParam(m_setting.getValueStr()) +
			'(' + std::to_string(m_paramControl->getPos()) + ')');
		m_setting.set(value, "overlay");
		getValueComboBox().setValue(value);
	}

	void SettingControl::onValueChanged()
	{
		D3dDdi::ScopedCriticalSection lock;
		const std::string value(getValueComboBox().getValue());
		m_setting.set(value, "overlay");
		m_paramLabel.reset();
		m_paramControl.reset();
		m_shaderStatusControl.reset();

		const auto paramInfo = m_setting.getParamInfo();
		const bool isCgp = "cgp" == paramInfo.name;
		RECT r = m_valueControl->getRect();
		r.right = r.left + (isCgp ? SHADER_SETTING_CONTROL_WIDTH : SETTING_CONTROL_WIDTH);
		m_valueControl->setRect(r);

		if (!paramInfo.name.empty())
		{
			r.left = r.right;
			if (isCgp)
			{
				r.right = m_rect.right - BORDER;
				m_shaderStatusControl.reset(new ShaderStatusControl(*this, r));
			}
			else
			{
				r.right = r.left + PARAM_LABEL_WIDTH;
				m_paramLabel.reset(new LabelControl(*this, r, paramInfo.name + ':', 0));
				r.left = r.right;
				r.right = r.left + PARAM_CONTROL_WIDTH;
				m_paramControl.reset(new ScrollBarControl(*this, r, paramInfo.min, paramInfo.max, m_setting.getParam()));
				m_paramControl->setPos(m_setting.getParam());
			}
		}
	}
}
