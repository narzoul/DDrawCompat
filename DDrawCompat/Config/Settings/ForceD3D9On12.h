#pragma once

#include <Config/EnumSetting.h>

namespace Config
{
	namespace Settings
	{
		class ForceD3D9On12 : public EnumSetting
		{
		public:
			enum Values { FORCEOFF, OFF, ON };

			ForceD3D9On12()
				: EnumSetting("ForceD3D9On12", "off", { "forceoff", "off", "on" })
			{
			}
		};
	}

	extern Settings::ForceD3D9On12 forceD3D9On12;
}
