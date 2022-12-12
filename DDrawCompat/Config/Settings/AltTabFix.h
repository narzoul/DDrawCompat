#pragma once

#include <Config/EnumSetting.h>

namespace Config
{
	namespace Settings
	{
		class AltTabFix : public MappedSetting<UINT>
		{
		public:
			static const UINT OFF = 0;
			static const UINT KEEPVIDMEM = 1;

			AltTabFix()
				: MappedSetting("AltTabFix", "off", {
					{"off", OFF},
					{"keepvidmem", KEEPVIDMEM}
					})
			{
			}
		};
	}

	extern Settings::AltTabFix altTabFix;
}
