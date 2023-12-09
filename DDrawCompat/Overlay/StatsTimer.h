#pragma once

#include <Overlay/StatsQueue.h>

class StatsTimer : public StatsQueue
{
public:
	StatsTimer();

	void start() { if (isEnabled()) { startImpl(); } }
	void stop() { if (isEnabled()) { stopImpl(); } }

private:
	virtual double convert(double stat) override;
	virtual void finalize(SampleCount& sampleCount, Stat& sum, Stat& min, Stat& max) override;
	virtual void resetTickCount() override;

	void startImpl();
	void stopImpl();

	long long m_qpcStart;
	long long m_qpcSum;
};
