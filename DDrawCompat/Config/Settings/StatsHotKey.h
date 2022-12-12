#pragma once

#include <Config/HotKeySetting.h>

namespace Config
{
	namespace Settings
	{
		class StatsHotKey : public HotKeySetting
		{
		public:
			StatsHotKey() : HotKeySetting("StatsHotKey", "shift+f12") {}
		};
	}

	extern Settings::StatsHotKey statsHotKey;
}
