#pragma once

#include <Config/MappedSetting.h>

namespace Config
{
	namespace Settings
	{
		class LogLevel : public MappedSetting<UINT>
		{
		public:
			static const UINT NONE = 0;
			static const UINT INFO = 1;
			static const UINT DEBUG = 2;
			static const UINT INITIAL = MAXUINT;

			LogLevel::LogLevel()
				: MappedSetting("LogLevel",
#ifdef _DEBUG
					"debug",
#else
					"info",
#endif
					{ {"none", NONE}, {"info", INFO}, {"debug", DEBUG} })
			{
			}
		};
	}
}
