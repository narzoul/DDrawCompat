#include <Config/Settings/FpsLimiter.h>

namespace Config
{
	namespace Settings
	{
		FpsLimiter::FpsLimiter()
			: EnumSetting("FpsLimiter", "off", { "off", "flipstart", "flipend", "msgloop" })
		{
		}

		Setting::ParamInfo FpsLimiter::getParamInfo() const
		{
			if (OFF != m_value)
			{
				return { "MaxFPS", 10, 200, 60 };
			}
			return {};
		}
	}
}
