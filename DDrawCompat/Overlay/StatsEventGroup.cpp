#include <Overlay/StatsEventGroup.h>

StatsEventGroup::StatsEventGroup()
	: m_rate(m_time)
{
}

void StatsEventGroup::add()
{
	auto qpcNow = Time::queryPerformanceCounter();
	auto tickCount = StatsQueue::getTickCount(qpcNow);

	m_count.add(tickCount);
	m_time.add(tickCount, qpcNow);
}
