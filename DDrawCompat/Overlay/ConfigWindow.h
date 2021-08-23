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

	private:
		virtual RECT calculateRect(const RECT& monitorRect) const override;

		void addControl(Config::Setting& setting);

		std::list<SettingControl> m_controls;
	};
}
