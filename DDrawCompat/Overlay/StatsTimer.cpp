#include <Overlay/StatsTimer.h>

StatsTimer::StatsTimer()
	: m_qpcStart(0)
	, m_qpcSum(0)
{
}

double StatsTimer::convert(double stat)
{
	return 100 * stat * StatsQueue::TICKS_PER_SEC / Time::g_qpcFrequency;
}

void StatsTimer::finalize(SampleCount& sampleCount, Stat& sum, Stat& min, Stat& max)
{
	if (0 != m_qpcStart)
	{
		auto qpcTickEnd = getQpc(getCurrentTickCount() + 1);
		m_qpcSum += qpcTickEnd - m_qpcStart;
		m_qpcStart = qpcTickEnd;
	}

	sampleCount = 1;
	sum = m_qpcSum;
	min = m_qpcSum;
	max = m_qpcSum;
	m_qpcSum = 0;
}

void StatsTimer::resetTickCount()
{
	if (0 != m_qpcStart)
	{
		m_qpcStart = getQpc(getCurrentTickCount());
	}
	m_qpcSum = 0;
}

void StatsTimer::start()
{
	auto qpcStart = Time::queryPerformanceCounter();
	setTickCount(getTickCount(qpcStart));
	m_qpcStart = qpcStart;
}

void StatsTimer::stop()
{
	auto qpcEnd = Time::queryPerformanceCounter();
	setTickCount(getTickCount(qpcEnd));
	m_qpcSum += qpcEnd - m_qpcStart;
	m_qpcStart = 0;
}