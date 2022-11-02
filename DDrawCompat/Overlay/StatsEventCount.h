#pragma once

#include <Overlay/StatsQueue.h>

class StatsEventCount : public StatsQueue
{
public:
	StatsEventCount();

	void add(TickCount tickCount);

private:
	virtual void finalize(SampleCount& sampleCount, Stat& sum, Stat& min, Stat& max) override;
	virtual void resetTickCount() override;

	std::vector<SampleCount> m_sampleCounts;
	SampleCount m_sampleCount;
	SampleCount m_totalSampleCount;
};
