#pragma once

#include <deque>
#include <vector>

#include <Common/Time.h>

class StatsQueue
{
public:
	static const uint32_t TICKS_PER_SEC = 5;
	static const uint32_t HISTORY_TIME = 3;
	static const uint32_t HISTORY_SIZE = HISTORY_TIME * TICKS_PER_SEC;

	typedef uint32_t SampleCount;
	typedef uint64_t Stat;
	typedef uint64_t TickCount;

	struct Stats
	{
		double cur;
		double avg;
		double min;
		double max;
	};

	StatsQueue();

	void addSample(TickCount tickCount, Stat stat);
	Stats getStats(TickCount tickCount);

	TickCount getCurrentTickCount() const { return m_currentTickCount; }

	static long long getQpc(TickCount tickCount)
	{
		return tickCount * Time::g_qpcFrequency / TICKS_PER_SEC;
	}

	static TickCount getTickCount(long long qpc = Time::queryPerformanceCounter())
	{
		return qpc * TICKS_PER_SEC / Time::g_qpcFrequency;
	}

protected:
	virtual Stats getRawStats(TickCount tickCount);
	SampleCount getSampleCount(TickCount tickCount) const;
	void setTickCount(TickCount tickCount);

private:
	struct TimestampedStat
	{
		TickCount tickCount;
		Stat stat;
	};

	virtual double convert(double stat) { return stat; }
	virtual void finalize(SampleCount& /*sampleCount*/, Stat& /*sum*/, Stat& /*min*/, Stat& /*max*/) {}
	virtual void resetTickCount() {}

	double getAvg(Stat sum, SampleCount sampleCount) const;

	void push();
	void pushToMinMaxQueue(std::deque<TimestampedStat>& queue, Stat stat, bool compare(Stat, Stat));

	std::vector<Stat> m_sums;
	std::vector<SampleCount> m_sampleCounts;
	std::deque<TimestampedStat> m_minQueue;
	std::deque<TimestampedStat> m_maxQueue;
	TickCount m_currentTickCount;
	SampleCount m_sampleCount;
	Stat m_sum;
	Stat m_min;
	Stat m_max;
	SampleCount m_totalSampleCount;
	Stat m_totalSum;
};