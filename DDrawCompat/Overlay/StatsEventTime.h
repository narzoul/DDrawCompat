#pragma once

#include <Overlay/StatsQueue.h>

class StatsEventTime : public StatsQueue
{
public:
	StatsEventTime();

	void add(TickCount tickCount, long long qpcNow) { if (isEnabled()) { addImpl(tickCount, qpcNow); } }

private:
	friend class StatsEventRate;

	void addImpl(TickCount tickCount, long long qpcNow);

	virtual double convert(double stat) override;

	long long m_qpcLast;
};
