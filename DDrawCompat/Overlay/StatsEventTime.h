#pragma once

#include <Overlay/StatsQueue.h>

class StatsEventTime : public StatsQueue
{
public:
	StatsEventTime();

	void add(TickCount tickCount, long long qpcNow);

private:
	friend class StatsEventRate;

	virtual double convert(double stat) override;

	long long m_qpcLast;
};
