#include <Config/Settings/SpriteDetection.h>

namespace Config
{
	namespace Settings
	{
		SpriteDetection::SpriteDetection()
			: MappedSetting("SpriteDetection", "off", {
				{"off", OFF},
				{"zconst", ZCONST},
				{"zmax", ZMAX},
				{"point", POINT}
				})
		{
		}

		Setting::ParamInfo SpriteDetection::getParamInfo() const
		{
			if (ZMAX == m_value)
			{
				return { "ZMax", 0, 100, 0, m_param };
			}
			return {};
		}
	}
}
