#pragma once

#include <Config/MappedSetting.h>

namespace Config
{
	namespace Settings
	{
		class AlternatePixelCenter : public MappedSetting<float>
		{
		public:
			AlternatePixelCenter()
				: MappedSetting("AlternatePixelCenter", "off", { {"off", 0.0f}, {"on", -0.5f} })
			{
			}
		};
	}
}
