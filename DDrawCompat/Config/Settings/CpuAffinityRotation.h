#pragma once

#include <Config/EnumSetting.h>

namespace Config
{
	namespace Settings
	{
		class CpuAffinityRotation : public MappedSetting<bool>
		{
		public:
			CpuAffinityRotation()
				: MappedSetting("CpuAffinityRotation", "on", { {"off", false}, {"on", true} })
			{
			}
		};
	}
}
