#pragma once

#include <Config/BoolSetting.h>

namespace Config
{
	namespace Settings
	{
		class CpuAffinityRotation : public BoolSetting
		{
		public:
			CpuAffinityRotation()
				: BoolSetting("CpuAffinityRotation", "on")
			{
			}
		};
	}

	extern Settings::CpuAffinityRotation cpuAffinityRotation;
}
