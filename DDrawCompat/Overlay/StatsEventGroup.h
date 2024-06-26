#pragma once

#include <Overlay/StatsEventCount.h>
#include <Overlay/StatsEventRate.h>
#include <Overlay/StatsEventTime.h>

class StatsEventGroup
{
public:
	StatsEventGroup();

	void add() { if (m_isEnabled) { addImpl(); } }
	void enable();

	StatsEventCount m_count;
	StatsEventTime m_time;
	StatsEventRate m_rate;

private:
	void addImpl();

	bool m_isEnabled;
};
