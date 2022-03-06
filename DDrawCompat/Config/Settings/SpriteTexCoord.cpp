#include <Config/Settings/SpriteTexCoord.h>

namespace Config
{
	namespace Settings
	{
		SpriteTexCoord::SpriteTexCoord()
			: MappedSetting("SpriteTexCoord", "app", {
				{"app", APP},
				{"clamp", CLAMP},
				{"clampall", CLAMPALL},
				{"round", ROUND}})
		{
		}

		Setting::ParamInfo SpriteTexCoord::getParamInfo() const
		{
			if (ROUND == m_value)
			{
				return { "Offset", -50, 50, 0, m_param };
			}
			return {};
		}
	}
}
