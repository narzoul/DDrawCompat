#pragma once

#include <Config/BoolSetting.h>

namespace Config
{
	namespace Settings
	{
		class ForceD3D9On12 : public BoolSetting
		{
		public:
			ForceD3D9On12()
				: BoolSetting("ForceD3D9On12", "off")
			{
			}
		};
	}

	extern Settings::ForceD3D9On12 forceD3D9On12;
}
