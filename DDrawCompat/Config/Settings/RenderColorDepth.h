#pragma once

#include <Config/MappedSetting.h>

namespace Config
{
	namespace Settings
	{
		class RenderColorDepth : public MappedSetting<unsigned>
		{
		public:
			RenderColorDepth()
				: MappedSetting("RenderColorDepth", "app", { {"app", 0}, {"16", 16}, {"32", 32} })
			{
			}
		};
	}
}
