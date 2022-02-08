#pragma once

#include <list>

#include <Overlay/SettingControl.h>
#include <Overlay/Window.h>

namespace Overlay
{
	class ConfigWindow : public Window
	{
	public:
		ConfigWindow();

		virtual void setVisible(bool isVisible) override;

		void setFocus(SettingControl* control);

	private:
		virtual RECT calculateRect(const RECT& monitorRect) const override;

		void addControl(Config::Setting& setting);

		std::list<SettingControl> m_controls;
		SettingControl* m_focus;
	};
}
