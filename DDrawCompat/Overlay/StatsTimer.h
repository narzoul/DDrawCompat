#pragma once

#include <Overlay/StatsQueue.h>

class StatsTimer : public StatsQueue
{
public:
	StatsTimer();

	void start();
	void stop();

private:
	virtual double convert(double stat) override;
	virtual void finalize(SampleCount& sampleCount, Stat& sum, Stat& min, Stat& max) override;
	virtual void resetTickCount() override;

	long long m_qpcStart;
	long long m_qpcSum;
};
