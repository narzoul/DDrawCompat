#include <Config/Settings/FpsLimiter.h>

namespace Config
{
	namespace Settings
	{
		FpsLimiter::FpsLimiter()
			: MappedSetting("FpsLimiter", "off", {
				{"off", OFF},
				{"flipstart", FLIPSTART},
				{"flipend", FLIPEND},
				{"msgloop", MSGLOOP}
				})
		{
		}

		Setting::ParamInfo FpsLimiter::getParamInfo() const
		{
			if (OFF != m_value)
			{
				return { "MaxFPS", 10, 200, 60, m_param };
			}
			return {};
		}
	}
}
