#pragma once

#include <Config/EnumSetting.h>

namespace Config
{
	namespace Settings
	{
		class ForceD3D9On12 : public MappedSetting<bool>
		{
		public:
			ForceD3D9On12()
				: MappedSetting("ForceD3D9On12", "off", { {"off", false}, {"on", true} })
			{
			}
		};
	}

	extern Settings::ForceD3D9On12 forceD3D9On12;
}
