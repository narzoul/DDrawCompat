#pragma once

#include <Config/EnumSetting.h>

namespace Config
{
	namespace Settings
	{
		class RemoveBorders : public MappedSetting<bool>
		{
		public:
			RemoveBorders()
				: MappedSetting("RemoveBorders", "off", { {"off", false}, {"on", true} })
			{
			}
		};
	}
}
