#pragma once

#include <array>
#include <functional>
#include <list>

#include <Overlay/LabelControl.h>
#include <Overlay/StatsQueue.h>

namespace Overlay
{
	class StatsWindow;

	class StatsControl : public Control
	{
	public:
		typedef std::function<std::array<std::string, 4>(StatsQueue::TickCount)> UpdateFunc;

		StatsControl(StatsWindow& parent, const RECT& rect, const std::string& caption, UpdateFunc updateFunc, DWORD style);

		void update(StatsQueue::TickCount tickCount);

		static int getColumnWidth(unsigned index);
		static int getWidth();

	private:
		std::list<LabelControl> m_labels;
		UpdateFunc m_updateFunc;
	};
}
