#pragma once

#include <Config/BoolSetting.h>

namespace Config
{
	namespace Settings
	{
		class RemoveBorders : public BoolSetting
		{
		public:
			RemoveBorders()
				: BoolSetting("RemoveBorders", "off")
			{
			}
		};
	}

	extern Settings::RemoveBorders removeBorders;
}
