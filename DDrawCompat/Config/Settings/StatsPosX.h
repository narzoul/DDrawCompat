#pragma once

#pragma once

#include <Config/MappedSetting.h>

namespace Config
{
	namespace Settings
	{
		class StatsPosX : public MappedSetting<int>
		{
		public:
			static const int CUSTOM = -1;

			StatsPosX()
				: MappedSetting("StatsPosX", "right", {
					{"left", 0},
					{"center", 50},
					{"right", 100},
					{"custom", CUSTOM}
					})
			{
			}

			int get() const
			{
				return CUSTOM == m_value ? m_param : m_value;
			}

			virtual ParamInfo getParamInfo() const override
			{
				return CUSTOM == m_value ? ParamInfo{ "Position", 0, 100, 100 } : ParamInfo{};
			}
		};
	}

	extern Settings::StatsPosX statsPosX;
}
