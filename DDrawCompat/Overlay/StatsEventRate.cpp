#pragma once

#include <Overlay/StatsEventRate.h>
#include <Overlay/StatsEventTime.h>

StatsEventRate::StatsEventRate(StatsEventTime& parent)
	: m_parent(parent)
{
}


StatsQueue::Stats StatsEventRate::getRawStats(TickCount tickCount)
{
	auto stats = m_parent.getRawStats(tickCount);
	std::swap(stats.min, stats.max);
	return stats;
}

double StatsEventRate::convert(double stat)
{
	return Time::g_qpcFrequency / stat;
}
