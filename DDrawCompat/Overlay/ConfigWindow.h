#pragma once

#include <functional>
#include <memory>
#include <set>
#include <string>

#include <D3dDdi/ShaderCompiler.h>
#include <Overlay/ButtonControl.h>
#include <Overlay/LabelControl.h>
#include <Overlay/ScrollBarControl.h>
#include <Overlay/SettingControl.h>
#include <Overlay/Window.h>

namespace Overlay
{
	class ConfigWindow : public Window
	{
	public:
		ConfigWindow();

		virtual void onMouseWheel(POINT pos, SHORT delta) override;
		virtual void onNotify(Control& control) override;
		virtual void setVisible(bool isVisible) override;

		const std::vector<D3dDdi::ShaderCompiler::Parameter>& getShaderParameters() const { return m_shaderParameters; }
		void invalidateShaderStatus() const;
		void setFocus(Control* control);
		void updateButtons();
		void updateDisplayFilter();

		static std::set<std::string> getRwSettingNames();

	private:
		static void onClose(ButtonControl& button);
		static void onExport(ButtonControl& button);
		static void onImport(ButtonControl& button);
		static void onResetAll(ButtonControl& button);

		virtual RECT calculateRect(const RECT& monitorRect) const override;

		std::unique_ptr<ButtonControl> addButton(const std::string& label, ButtonControl::ClickHandler clickHandler);
		void addSettingControls();
		std::string constructFileContent();
		void exportSettings();
		RECT getNextSettingControlRect() const;
		void importSettings();
		void resetSettings();
		void updateSettings(std::function<std::string(const Config::Setting&)> getValue);

		unsigned m_buttonCount;
		std::unique_ptr<LabelControl> m_caption;
		std::unique_ptr<ButtonControl> m_captionCloseButton;
		std::unique_ptr<ButtonControl> m_closeButton;
		std::unique_ptr<ButtonControl> m_exportButton;
		std::unique_ptr<ButtonControl> m_importButton;
		std::unique_ptr<ButtonControl> m_resetAllButton;
		std::unique_ptr<ScrollBarControl> m_scrollBar;
		SettingControl* m_displayFilterSettingControl;
		std::vector<std::unique_ptr<Control>> m_settingControls;
		std::vector<D3dDdi::ShaderCompiler::Parameter> m_shaderParameters;
		Control* m_focus;
		std::string m_fileContent;
	};
}
