#pragma once

#include <Config/MappedSetting.h>

namespace Config
{
	namespace Settings
	{
		class DpiAwareness : public MappedSetting<DPI_AWARENESS_CONTEXT>
		{
		public:
			DpiAwareness()
				: MappedSetting("DpiAwareness", "permonitor", {
					{"app", nullptr},
					{"unaware", DPI_AWARENESS_CONTEXT_UNAWARE},
					{"system", DPI_AWARENESS_CONTEXT_SYSTEM_AWARE},
					{"permonitor", DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE},
					{"permonitorv2", DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2},
					{"gdiscaled", DPI_AWARENESS_CONTEXT_UNAWARE_GDISCALED}
					})
			{
			}
		};
	}
}
