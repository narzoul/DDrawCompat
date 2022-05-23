#pragma once

#include <list>

#include <Overlay/ButtonControl.h>
#include <Overlay/LabelControl.h>
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
		static void onClose(Control& control);

		virtual RECT calculateRect(const RECT& monitorRect) const override;

		void addControl(Config::Setting& setting);

		LabelControl m_caption;
		ButtonControl m_closeButton;
		std::list<SettingControl> m_controls;
		SettingControl* m_focus;
	};
}
