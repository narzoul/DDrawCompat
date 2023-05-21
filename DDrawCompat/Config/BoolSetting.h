#pragma once

#include <Config/EnumSetting.h>

namespace Config
{
	namespace Settings
	{
		class BoolSetting : public EnumSetting
		{
		public:
			BoolSetting(const std::string& name, const std::string& default)
				: EnumSetting(name, default, { "off", "on" })
			{
			}
		};
	}
}
