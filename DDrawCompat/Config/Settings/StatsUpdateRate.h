#pragma once

#include <Config/IntSetting.h>

namespace Config
{
	namespace Settings
	{
		class StatsUpdateRate : public IntSetting
		{
		public:
			StatsUpdateRate()
				: IntSetting("StatsUpdateRate", "5", 1, 10)
			{
			}
		};
	}

	extern Settings::StatsUpdateRate statsUpdateRate;
}
