#pragma once

#pragma once

#include <Config/MappedSetting.h>

namespace Config
{
	namespace Settings
	{
		class ConfigTransparency : public MappedSetting<int>
		{
		public:
			static const int ALPHA = -1;

			ConfigTransparency()
				: MappedSetting("ConfigTransparency", "alpha(90)", { {"off", 100}, {"alpha", ALPHA} })
			{
			}

			int get() const
			{
				return ALPHA == m_value ? m_param : m_value;
			}

			virtual ParamInfo getParamInfo() const override
			{
				return ALPHA == m_value ? ParamInfo{ "Alpha", 25, 100, 90 } : ParamInfo{};
			}
		};
	}

	extern Settings::ConfigTransparency configTransparency;
}
