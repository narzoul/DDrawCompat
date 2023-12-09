#pragma once

#include <Overlay/StatsEventCount.h>

StatsEventCount::StatsEventCount()
	: m_sampleCounts(s_update_rate)
	, m_sampleCount(0)
	, m_totalSampleCount(0)
{
}

void StatsEventCount::finalize(SampleCount& sampleCount, Stat& sum, Stat& min, Stat& max)
{
	const uint32_t index = getCurrentTickCount() % s_update_rate;
	m_totalSampleCount += m_sampleCount;
	m_totalSampleCount -= m_sampleCounts[index];
	m_sampleCounts[index] = m_sampleCount;
	m_sampleCount = 0;

	sum = m_totalSampleCount;
	min = m_totalSampleCount;
	max = m_totalSampleCount;
	sampleCount = 1;
}

void StatsEventCount::resetTickCount()
{
	std::fill(m_sampleCounts.begin(), m_sampleCounts.end(), 0);
	m_sampleCount = 0;
	m_totalSampleCount = 0;
}
