#pragma once

#include <Config/MappedSetting.h>

namespace Config
{
	namespace Settings
	{
		class RenderColorDepth : public MappedSetting<std::pair<unsigned, unsigned>>
		{
		public:
			RenderColorDepth()
				: MappedSetting("RenderColorDepth", "32", {
					{ "app", { 0, 8 } },
					{ "appd8", { 8, 0 } },
					{ "appd10", { 10, 0 } },
					{ "16", { 6, 8 } },
					{ "16d8", { 8, 6 } },
					{ "16d10", { 10, 6 } },
					{ "32", { 8, 8 } },
					{ "32d10", { 10, 8 } },
					})
			{
			}
		};
	}

	extern Settings::RenderColorDepth renderColorDepth;
}
