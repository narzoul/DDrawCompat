#pragma once

#include <Config/EnumSetting.h>

namespace Config
{
	namespace Settings
	{
		class FontAntialiasing : public EnumSetting
		{
		public:
			enum Values { APP, OFF, ON };

			FontAntialiasing()
				: EnumSetting("FontAntialiasing", "app", { "app", "off", "on" })
			{
			}
		};
	}

	extern Settings::FontAntialiasing fontAntialiasing;
}
