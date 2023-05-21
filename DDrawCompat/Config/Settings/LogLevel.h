#pragma once

#include <Config/EnumSetting.h>

namespace Config
{
	namespace Settings
	{
		class LogLevel : public EnumSetting
		{
		public:
			enum Values { NONE, INFO, DEBUG };

			LogLevel::LogLevel()
				: EnumSetting("LogLevel",
#ifdef _DEBUG
					"debug",
#else
					"info",
#endif
					{ "none", "info", "debug" })
			{
			}
		};
	}

	extern Settings::LogLevel logLevel;
}
