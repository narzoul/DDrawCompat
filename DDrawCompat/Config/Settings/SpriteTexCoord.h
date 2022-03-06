#pragma once

#include <Config/MappedSetting.h>

namespace Config
{
	namespace Settings
	{
		class SpriteTexCoord : public MappedSetting<UINT>
		{
		public:
			static const UINT APP = 0;
			static const UINT CLAMP = 1;
			static const UINT CLAMPALL = 2;
			static const UINT ROUND = 3;

			SpriteTexCoord();

			virtual ParamInfo getParamInfo() const override;
		};
	}
}
