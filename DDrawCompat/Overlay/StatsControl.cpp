#include <Config/Settings/StatsColumns.h>
#include <Overlay/StatsWindow.h>
#include <Overlay/StatsControl.h>

namespace
{
	const int NAME_LABEL_WIDTH = 70;
	const int VALUE_LABEL_WIDTH = 40;
}

namespace Overlay
{
	StatsControl::StatsControl(StatsWindow& parent, const RECT& rect, const std::string& caption, UpdateFunc updateFunc, DWORD style)
		: Control(&parent, rect, style)
		, m_updateFunc(updateFunc)
	{
		auto& columns = Config::statsColumns.get();
		RECT r = rect;
		for (unsigned i = 0; i < columns.size(); ++i)
		{
			r.right = r.left + getColumnWidth(i);
			if (Config::Settings::StatsColumns::LABEL == columns[i])
			{
				m_labels.emplace_back(*this, r, caption, 0, WS_DISABLED | WS_VISIBLE);
			}
			else
			{
				m_labels.emplace_back(*this, r, std::string(), DT_RIGHT);
			}
			r.left = r.right;
		}
	}

	int StatsControl::getColumnWidth(unsigned index)
	{
		auto& columns = Config::statsColumns.get();
		if (index >= columns.size())
		{
			return 0;
		}
		return Config::Settings::StatsColumns::LABEL == columns[index] ? NAME_LABEL_WIDTH : VALUE_LABEL_WIDTH;
	}

	int StatsControl::getWidth()
	{
		int width = 0;
		auto& columns = Config::statsColumns.get();
		for (unsigned i = 0; i < columns.size(); ++i)
		{
			width += getColumnWidth(i);
		}
		return width;
	}

	void StatsControl::update(StatsQueue::TickCount tickCount)
	{
		auto stats = m_updateFunc(tickCount);
		auto& columns = Config::statsColumns.get();
		auto label = m_labels.begin();
		for (unsigned i = 0; i < columns.size(); ++i)
		{
			if (Config::Settings::StatsColumns::LABEL != columns[i])
			{
				label->setLabel(stats[columns[i]]);
			}
			++label;
		}
	}
}
