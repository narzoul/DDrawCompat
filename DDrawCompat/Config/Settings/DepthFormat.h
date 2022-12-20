#pragma once

#include <Config/MappedSetting.h>

namespace Config
{
	namespace Settings
	{
		class DepthFormat : public MappedSetting<UINT>
		{
		public:
			static const UINT APP = 0;

			DepthFormat() : MappedSetting("DepthFormat", "app", { {"app", APP}, {"16", 16}, {"24", 24}, {"32", 32} })
			{
			}
		};
	}

	extern Settings::DepthFormat depthFormat;
}
