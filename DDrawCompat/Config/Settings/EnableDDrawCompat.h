#pragma once

#include <Config/BoolSetting.h>

namespace Config
{
	namespace Settings
	{
		class EnableDDrawCompat : public BoolSetting
		{
		public:
			EnableDDrawCompat()
				: BoolSetting("EnableDDrawCompat", "on")
			{
			}
		};
	}

	extern Settings::EnableDDrawCompat enableDDrawCompat;
}
