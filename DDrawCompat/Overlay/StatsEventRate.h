#pragma once

#include <Overlay/StatsQueue.h>

class StatsEventTime;

class StatsEventRate : public StatsQueue
{
public:
	StatsEventRate(StatsEventTime& parent);

private:
	virtual double convert(double stat) override;
	virtual Stats getRawStats(TickCount tickCount) override;

	StatsEventTime& m_parent;
};
