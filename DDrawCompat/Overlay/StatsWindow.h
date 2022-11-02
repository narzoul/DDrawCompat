#pragma once

#include <list>
#include <memory>
#include <string>

#include <Overlay/StatsControl.h>
#include <Overlay/StatsEventGroup.h>
#include <Overlay/StatsQueue.h>
#include <Overlay/StatsTimer.h>
#include <Overlay/Window.h>

namespace Overlay
{
	class StatsWindow : public Window
	{
	public:
		StatsWindow();

		void updateStats();

		StatsEventGroup m_present;
		StatsEventGroup m_flip;
		StatsEventGroup m_blit;
		StatsEventGroup m_lock;
		StatsTimer m_ddiUsage;
		StatsQueue m_gdiObjects;

	private:
		StatsControl& addControl(const std::string& name, StatsControl::UpdateFunc updateFunc, DWORD style = WS_VISIBLE);

		virtual RECT calculateRect(const RECT& monitorRect) const override;
		virtual HWND getTopmost() const override;

		std::list<StatsControl> m_statsControls;
		uint64_t m_tickCount;
	};
}
