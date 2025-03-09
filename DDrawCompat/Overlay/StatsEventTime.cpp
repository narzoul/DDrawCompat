#pragma once

#include <Overlay/StatsEventTime.h>

StatsEventTime::StatsEventTime()
	: m_qpcLast(0)
{
}

void StatsEventTime::addImpl(TickCount tickCount, long long qpcNow)
{
	Compat::ScopedCriticalSection lock(m_cs);
	if (0 != m_qpcLast && qpcNow - m_qpcLast < s_history_time * Time::g_qpcFrequency)
	{
		addSample(tickCount, qpcNow - m_qpcLast);
	}
	m_qpcLast = qpcNow;
}

double StatsEventTime::convert(double stat)
{
	return 1000 * stat / Time::g_qpcFrequency;
}
