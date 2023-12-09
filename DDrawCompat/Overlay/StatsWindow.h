#pragma once

#include <array>
#include <list>
#include <memory>
#include <string>
#include <vector>

#include <Config/Settings/StatsRows.h>
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

		bool isRowEnabled(Config::Settings::StatsRows::Values row) const { return m_isRowEnabled[row]; }
		void updateStats();

		uint32_t m_presentCount;
		StatsEventGroup m_present;
		StatsEventGroup m_flip;
		StatsEventGroup m_blit;
		StatsEventGroup m_lock;
		StatsTimer m_ddiUsage;
		StatsQueue m_gdiObjects;

	private:
		struct StatsRow
		{
			const char* name;
			StatsControl::UpdateFunc updateFunc;
			StatsQueue* statsQueue;
			DWORD style;
		};

		StatsControl& addControl(const std::string& name, StatsControl::UpdateFunc updateFunc, DWORD style = WS_VISIBLE);

		virtual RECT calculateRect(const RECT& monitorRect) const override;
		virtual HWND getTopmost() const override;

		static LONG getWidth();

		std::array<bool, Config::Settings::StatsRows::VALUE_COUNT> m_isRowEnabled;
		std::list<StatsControl> m_statsControls;
		std::vector<StatsRow> m_statsRows;
		uint64_t m_tickCount;
	};
}
