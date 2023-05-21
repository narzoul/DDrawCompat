#include <Config/Settings/SpriteTexCoord.h>

namespace Config
{
	namespace Settings
	{
		SpriteTexCoord::SpriteTexCoord()
			: EnumSetting("SpriteTexCoord", "app", { "app", "clamp", "clampall", "round" })
		{
		}

		Setting::ParamInfo SpriteTexCoord::getParamInfo() const
		{
			if (ROUND == m_value)
			{
				return { "Offset", -50, 50, 0 };
			}
			return {};
		}
	}
}
