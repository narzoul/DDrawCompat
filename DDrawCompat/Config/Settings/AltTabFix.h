#pragma once

#include <Config/EnumSetting.h>

namespace Config
{
	namespace Settings
	{
		class AltTabFix : public EnumSetting
		{
		public:
			enum Values { OFF, KEEPVIDMEM, KEEPVIDMEMNP };

			AltTabFix()
				: EnumSetting("AltTabFix", "off", { "off", "keepvidmem", "keepvidmemnp" })
			{
			}
		};
	}

	extern Settings::AltTabFix altTabFix;
}
