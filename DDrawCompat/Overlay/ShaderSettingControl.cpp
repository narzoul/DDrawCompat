#include <sstream>

#include <Config/Settings/DisplayFilter.h>
#include <D3dDdi/Device.h>
#include <D3dDdi/ScopedCriticalSection.h>
#include <Overlay/ConfigWindow.h>
#include <Overlay/ShaderSettingControl.h>

namespace Overlay
{
	ShaderSettingControl::ShaderSettingControl(
		ConfigWindow& parent, const RECT& rect, D3dDdi::ShaderCompiler::Parameter& parameter)
		: Control(&parent, rect, WS_VISIBLE | WS_TABSTOP)
		, m_param(parameter)
		, m_descriptionLabel(*this, { rect.left, rect.top, rect.left + DESC_LABEL_WIDTH, rect.bottom },
			parameter.description + ':', TA_RIGHT)
		, m_valueLabel(*this,
			{
				rect.left + DESC_LABEL_WIDTH,
				rect.top + BORDER / 2,
				rect.left + DESC_LABEL_WIDTH + VALUE_LABEL_WIDTH,
				rect.bottom - BORDER / 2
			},
			"", TA_RIGHT, WS_VISIBLE | WS_BORDER)
		, m_valueControl(*this,
			{
				rect.left + m_valueLabel.getRect().right + BORDER,
				rect.top + BORDER / 2,
				rect.right - BORDER,
				rect.bottom - BORDER / 2
			},
			static_cast<int>(std::round(parameter.min / parameter.step)),
			static_cast<int>(std::round(parameter.max / parameter.step)),
			static_cast<int>(std::round(parameter.currentValue / parameter.step)))
	{
		updateValueLabel();
	}

	RECT ShaderSettingControl::getHighlightRect() const
	{
		RECT r = m_rect;
		InflateRect(&r, -BORDER / 2, 0);
		return r;
	}

	void ShaderSettingControl::onLButtonDown(POINT pos)
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

	void ShaderSettingControl::onMouseWheel(POINT pos, SHORT delta)
	{
		m_valueControl.onMouseWheel(pos, delta);
	}

	void ShaderSettingControl::onNotify(Control& /*control*/)
	{
		m_param.currentValue = m_valueControl.getPos() * m_param.step;
		m_param.currentValue = std::max(m_param.currentValue, m_param.min);
		m_param.currentValue = std::min(m_param.currentValue, m_param.max);

		Config::displayFilter.setCgpParameters(static_cast<ConfigWindow*>(m_parent)->getShaderParameters());
		updateValueLabel();

		D3dDdi::ScopedCriticalSection lock;
		for (auto& device : D3dDdi::Device::getDevices())
		{
			device.second.getShaderBlitter().getMetaShader().updateParameters();
		}
	}

	void ShaderSettingControl::updateValueLabel()
	{
		char buf[20] = {};
		sprintf_s(buf, "%.3f", m_param.currentValue);
		m_valueLabel.setLabel(buf);
		invalidate();
	}
}
