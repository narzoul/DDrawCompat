#pragma once

#include <Config/MappedSetting.h>

namespace Config
{
	namespace Settings
	{
		class SpriteDetection : public MappedSetting<UINT>
		{
		public:
			static const UINT OFF = 0;
			static const UINT ZCONST = 1;
			static const UINT ZMAX = 2;
			static const UINT POINT = 3;

			SpriteDetection();

			virtual ParamInfo getParamInfo() const override;
		};
	}

	extern Settings::SpriteDetection spriteDetection;
}
