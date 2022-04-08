#pragma once

#include <Config/MappedSetting.h>

namespace Config
{
	namespace Settings
	{
		class FullscreenMode : public MappedSetting<UINT>
		{
		public:
			static const UINT BORDERLESS = 0;
			static const UINT EXCLUSIVE = 1;

			FullscreenMode()
				: MappedSetting("FullscreenMode", "borderless", { {"borderless", BORDERLESS}, {"exclusive", EXCLUSIVE} })
			{
			}
		};
	}
}
