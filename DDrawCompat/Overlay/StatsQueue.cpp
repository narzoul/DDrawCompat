#include <Config/Settings/StatsUpdateRate.h>
#include <Overlay/StatsQueue.h>

namespace
{
	bool greaterEqual(StatsQueue::Stat a, StatsQueue::Stat b)
	{
		return a >= b;
	}

	bool lessEqual(StatsQueue::Stat a, StatsQueue::Stat b)
	{
		return a <= b;
	}
}

StatsQueueInitializer::StatsQueueInitializer()
{
	s_update_rate = Config::statsUpdateRate.get();
	s_history_time = 3;
	s_history_size = s_history_time * s_update_rate;
}

uint32_t StatsQueueInitializer::s_history_size = 0;
uint32_t StatsQueueInitializer::s_history_time = 0;
uint32_t StatsQueueInitializer::s_update_rate = 0;

StatsQueue::StatsQueue()
	: m_sums(s_history_size)
	, m_sampleCounts(s_history_size)
	, m_currentTickCount(0)
	, m_sampleCount(0)
	, m_sum(0)
	, m_min(0)
	, m_max(0)
	, m_totalSampleCount(0)
	, m_totalSum(0)
{
}

void StatsQueue::addSample(TickCount tickCount, Stat stat)
{
	setTickCount(tickCount);

	if (0 == m_sampleCount)
	{
		m_min = stat;
		m_max = stat;
	}
	else if (stat < m_min)
	{
		m_min = stat;
	}
	else if (stat > m_max)
	{
		m_max = stat;
	}

	++m_sampleCount;
	m_sum += stat;
}

double StatsQueue::getAvg(Stat sum, SampleCount sampleCount) const
{
	return 0 == sampleCount ? NAN : (static_cast<double>(sum) / sampleCount);
}

StatsQueue::Stats StatsQueue::getRawStats(TickCount tickCount)
{
	setTickCount(tickCount);
	Stats stats = {};
	const uint32_t index = (m_currentTickCount - 1) % s_history_size;
	stats.cur = getAvg(m_sums[index], m_sampleCounts[index]);
	stats.avg = getAvg(m_totalSum, m_totalSampleCount);
	stats.min = m_minQueue.empty() ? NAN : m_minQueue.front().stat;
	stats.max = m_maxQueue.empty() ? NAN : m_maxQueue.front().stat;
	return stats;
}

StatsQueue::SampleCount StatsQueue::getSampleCount(TickCount tickCount) const
{
	return m_sampleCounts[tickCount % s_history_size];
}

StatsQueue::Stats StatsQueue::getStats(TickCount tickCount)
{
	Stats stats = getRawStats(tickCount);
	stats.cur = convert(stats.cur);
	stats.avg = convert(stats.avg);
	stats.min = convert(stats.min);
	stats.max = convert(stats.max);
	return stats;
}

void StatsQueue::push()
{
	finalize(m_sampleCount, m_sum, m_min, m_max);

	uint32_t index = m_currentTickCount % s_history_size;
	m_totalSampleCount -= m_sampleCounts[index];
	m_totalSampleCount += m_sampleCount;
	m_totalSum -= m_sums[index];
	m_totalSum += m_sum;
	m_sampleCounts[index] = m_sampleCount;
	m_sums[index] = m_sum;

	pushToMinMaxQueue(m_minQueue, m_min, lessEqual);
	pushToMinMaxQueue(m_maxQueue, m_max, greaterEqual);

	m_sampleCount = 0;
	m_sum = 0;
	m_min = 0;
	m_max = 0;
	++m_currentTickCount;
}

void StatsQueue::pushToMinMaxQueue(std::deque<TimestampedStat>& queue, Stat stat, bool compare(Stat, Stat))
{
	if (0 != m_sampleCount)
	{
		while (!queue.empty() && compare(stat, queue.back().stat))
		{
			queue.pop_back();
		}
	}

	while (!queue.empty() && m_currentTickCount - queue.front().tickCount >= s_history_size)
	{
		queue.pop_front();
	}

	if (0 != m_sampleCount)
	{
		queue.push_back({ m_currentTickCount, stat });
	}
}

void StatsQueue::setTickCount(TickCount tickCount)
{
	if (tickCount - m_currentTickCount > s_history_size)
	{
		m_currentTickCount = tickCount - s_history_size;
		resetTickCount();
	}

	while (m_currentTickCount < tickCount)
	{
		push();
	}
}
