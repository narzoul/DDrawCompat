#pragma once

#include <Config/MappedSetting.h>

namespace Config
{
	namespace Settings
	{
		class SpriteFilter : public MappedSetting<UINT>
		{
		public:
			SpriteFilter();
		};
	}

	extern Settings::SpriteFilter spriteFilter;
}
