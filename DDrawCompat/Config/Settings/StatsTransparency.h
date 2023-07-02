#pragma once

#pragma once

#include <Config/MappedSetting.h>

namespace Config
{
	namespace Settings
	{
		class StatsTransparency : public MappedSetting<int>
		{
		public:
			static const int ALPHA = -1;

			StatsTransparency()
				: MappedSetting("StatsTransparency", "alpha(75)", { {"off", 100}, {"alpha", ALPHA} })
			{
			}

			int get() const
			{
				return ALPHA == m_value ? m_param : m_value;
			}

			virtual ParamInfo getParamInfo() const override
			{
				return ALPHA == m_value ? ParamInfo{ "Alpha", 25, 100, 75 } : ParamInfo{};
			}
		};
	}

	extern Settings::StatsTransparency statsTransparency;
}
