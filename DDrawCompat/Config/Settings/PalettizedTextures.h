#pragma once

#include <Config/BoolSetting.h>

namespace Config
{
	namespace Settings
	{
		class PalettizedTextures : public BoolSetting
		{
		public:
			PalettizedTextures()
				: BoolSetting("PalettizedTextures", "on")
			{
			}
		};
	}

	extern Settings::PalettizedTextures palettizedTextures;
}
