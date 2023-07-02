#pragma once

#pragma once

#include <Config/MappedSetting.h>

namespace Config
{
	namespace Settings
	{
		class StatsPosY : public MappedSetting<int>
		{
		public:
			static const int CUSTOM = -1;

			StatsPosY()
				: MappedSetting("StatsPosY", "top", {
					{"top", 0},
					{"center", 50},
					{"bottom", 100},
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
				return CUSTOM == m_value ? ParamInfo{ "Position", 0, 100, 0 } : ParamInfo{};
			}
		};
	}

	extern Settings::StatsPosY statsPosY;
}
