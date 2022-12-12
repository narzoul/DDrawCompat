#pragma once

#include <Config/EnumSetting.h>

namespace Config
{
	namespace Settings
	{
		class PalettizedTextures : public MappedSetting<bool>
		{
		public:
			PalettizedTextures()
				: MappedSetting("PalettizedTextures", "on", { {"off", false}, {"on", true} })
			{
			}
		};
	}

	extern Settings::PalettizedTextures palettizedTextures;
}
