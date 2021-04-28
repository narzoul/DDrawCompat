#pragma once

#include <Config/EnumSetting.h>

namespace Config
{
	namespace Settings
	{
		class ThreadPriorityBoost : public EnumSetting
		{
		public:
			enum Value { OFF, ON, MAIN, APP };

			ThreadPriorityBoost()
				: EnumSetting("ThreadPriorityBoost", OFF, { "off", "on", "main", "app" })
			{
			}
		};
	}
}
