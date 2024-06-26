#pragma once

#include <Config/EnumListSetting.h>

namespace Config
{
	namespace Settings
	{
		class StatsColumns : public EnumListSetting
		{
		public:
			enum Values { CUR, AVG, MIN, MAX, LABEL, VALUE_COUNT };

			StatsColumns()
				: EnumListSetting("StatsColumns", "label, cur, avg, min, max", { "cur", "avg", "min", "max", "label" })
			{
			}
		};
	}

	extern Settings::StatsColumns statsColumns;
}
