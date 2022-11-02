#include <Overlay/StatsWindow.h>
#include <Overlay/StatsControl.h>

namespace Overlay
{
	StatsControl::StatsControl(StatsWindow& parent, const RECT& rect, const std::string& caption, UpdateFunc updateFunc, DWORD style)
		: Control(&parent, rect, style)
		, m_captionLabel(*this, { rect.left, rect.top,
			rect.left + NAME_LABEL_WIDTH, rect.bottom }, caption, 0, WS_DISABLED | WS_VISIBLE)
		, m_curLabel(*this, { m_captionLabel.getRect().right, rect.top,
			m_captionLabel.getRect().right + VALUE_LABEL_WIDTH, rect.bottom}, std::string(), DT_RIGHT)
		, m_avgLabel(*this, { m_curLabel.getRect().right, rect.top,
			m_curLabel.getRect().right + VALUE_LABEL_WIDTH, rect.bottom }, std::string(), DT_RIGHT)
		, m_minLabel(*this, { m_avgLabel.getRect().right, rect.top,
			m_avgLabel.getRect().right + VALUE_LABEL_WIDTH, rect.bottom }, std::string(), DT_RIGHT)
		, m_maxLabel(*this, { m_minLabel.getRect().right, rect.top,
			m_minLabel.getRect().right + VALUE_LABEL_WIDTH, rect.bottom }, std::string(), DT_RIGHT)
		, m_updateFunc(updateFunc)
	{
	}

	void StatsControl::update(StatsQueue::TickCount tickCount)
	{
		auto stats = m_updateFunc(tickCount);
		m_curLabel.setLabel(stats[0]);
		m_avgLabel.setLabel(stats[1]);
		m_minLabel.setLabel(stats[2]);
		m_maxLabel.setLabel(stats[3]);
	}
}
