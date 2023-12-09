#include <Overlay/StatsEventGroup.h>

StatsEventGroup::StatsEventGroup()
	: m_rate(m_time)
	, m_isEnabled(false)
{
}

void StatsEventGroup::addImpl()
{
	auto qpcNow = Time::queryPerformanceCounter();
	auto tickCount = StatsQueue::getTickCount(qpcNow);

	m_count.add(tickCount);
	m_time.add(tickCount, qpcNow);
}

void StatsEventGroup::enable()
{
	if (m_rate.isEnabled())
	{
		m_time.enable();
	}

	m_isEnabled = m_count.isEnabled() || m_time.isEnabled() || m_rate.isEnabled();
}
