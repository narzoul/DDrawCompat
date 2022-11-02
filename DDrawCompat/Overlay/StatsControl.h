#pragma once

#include <array>
#include <functional>

#include <Overlay/LabelControl.h>
#include <Overlay/StatsQueue.h>

namespace Overlay
{
	class StatsWindow;

	class StatsControl : public Control
	{
	public:
		static const int NAME_LABEL_WIDTH = 70;
		static const int VALUE_LABEL_WIDTH = 40;

		typedef std::function<std::array<std::string, 4>(StatsQueue::TickCount)> UpdateFunc;

		StatsControl(StatsWindow& parent, const RECT& rect, const std::string& caption, UpdateFunc updateFunc, DWORD style);

		void update(StatsQueue::TickCount tickCount);

	private:
		LabelControl m_captionLabel;
		LabelControl m_curLabel;
		LabelControl m_avgLabel;
		LabelControl m_minLabel;
		LabelControl m_maxLabel;
		UpdateFunc m_updateFunc;
	};
}
