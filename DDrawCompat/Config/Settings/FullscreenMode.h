#pragma once

#include <Config/EnumSetting.h>

namespace Config
{
	namespace Settings
	{
		class FullscreenMode : public EnumSetting
		{
		public:
			static const UINT BORDERLESS = 0;
			static const UINT EXCLUSIVE = 1;

			FullscreenMode()
				: EnumSetting("FullscreenMode", "borderless", { "borderless", "exclusive" })
			{
			}
		};
	}

	extern Settings::FullscreenMode fullscreenMode;
}
