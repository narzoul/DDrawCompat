#pragma once

#include <Config/IntSetting.h>

namespace Config
{
	namespace Settings
	{
		class StatsAggregateTime : public IntSetting
		{
		public:
			StatsAggregateTime()
				: IntSetting("StatsAggregateTime", "3", 1, 60)
			{
			}
		};
	}

	extern Settings::StatsAggregateTime statsAggregateTime;
}
