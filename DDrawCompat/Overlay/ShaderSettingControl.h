#pragma once

#include <functional>
#include <string>

#include <D3dDdi/ShaderCompiler.h>
#include <Overlay/LabelControl.h>
#include <Overlay/ScrollBarControl.h>
#include <Overlay/SettingControl.h>

namespace Overlay
{
	class ComboBoxControl;
	class ConfigWindow;

	class ShaderSettingControl : public Control
	{
	public:
		static const int DESC_LABEL_WIDTH = SettingControl::SETTING_LABEL_WIDTH + SettingControl::SETTING_CONTROL_WIDTH;
		static const int VALUE_LABEL_WIDTH = SettingControl::PARAM_LABEL_WIDTH - BORDER;

		ShaderSettingControl(ConfigWindow& parent, const RECT& rect, D3dDdi::ShaderCompiler::Parameter& parameter);

		virtual RECT getHighlightRect() const override;
		virtual void onLButtonDown(POINT pos) override;
		virtual void onMouseWheel(POINT pos, SHORT delta) override;
		virtual void onNotify(Control& control) override;

	private:
		void updateValueLabel();

		D3dDdi::ShaderCompiler::Parameter& m_param;
		LabelControl m_descriptionLabel;
		LabelControl m_valueLabel;
		ScrollBarControl m_valueControl;
	};
}
