#pragma once

#include <Config/EnumSetting.h>

namespace Config
{
	namespace Settings
	{
		class SpriteTexCoord : public EnumSetting
		{
		public:
			enum Values { APP, CLAMP, CLAMPALL, ROUND };

			SpriteTexCoord();

			virtual ParamInfo getParamInfo() const override;
		};
	}

	extern Settings::SpriteTexCoord spriteTexCoord;
}
