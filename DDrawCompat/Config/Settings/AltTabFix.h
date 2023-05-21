#pragma once

#include <Config/EnumSetting.h>

namespace Config
{
	namespace Settings
	{
		class AltTabFix : public EnumSetting
		{
		public:
			enum Values { OFF, KEEPVIDMEM };

			AltTabFix()
				: EnumSetting("AltTabFix", "off", { "off", "keepvidmem" })
			{
			}
		};
	}

	extern Settings::AltTabFix altTabFix;
}
