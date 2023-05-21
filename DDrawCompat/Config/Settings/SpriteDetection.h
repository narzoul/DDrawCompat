#pragma once

#include <Config/EnumSetting.h>

namespace Config
{
	namespace Settings
	{
		class SpriteDetection : public EnumSetting
		{
		public:
			enum Values { OFF, ZCONST, ZMAX, POINT };

			SpriteDetection();

			virtual ParamInfo getParamInfo() const override;
		};
	}

	extern Settings::SpriteDetection spriteDetection;
}
