#pragma once

#include <functional>
#include <list>
#include <memory>
#include <set>
#include <string>

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

		void setFocus(SettingControl* control);
		void updateButtons();

		static std::set<std::string> getRwSettingNames();

	private:
		static void onClose(Control& control);
		static void onExport(Control& control);
		static void onImport(Control& control);
		static void onResetAll(Control& control);

		virtual RECT calculateRect(const RECT& monitorRect) const override;

		std::unique_ptr<ButtonControl> addButton(const std::string& label, ButtonControl::ClickHandler clickHandler);
		void addSettingControl(Config::Setting& setting, SettingControl::UpdateFunc updateFunc, bool isReadOnly);
		void addSettingControls();
		std::string constructFileContent();
		void exportSettings();
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
		std::list<SettingControl> m_settingControls;
		SettingControl* m_focus;
		std::string m_fileContent;
	};
}
