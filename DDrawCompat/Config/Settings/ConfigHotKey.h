#pragma once

#include <Config/HotKeySetting.h>

namespace Config
{
	namespace Settings
	{
		class ConfigHotKey : public HotKeySetting
		{
		public:
			ConfigHotKey() : HotKeySetting("ConfigHotKey", "shift+f11") {}
		};
	}
}
