#pragma once

#include <Config/MappedSetting.h>

namespace Config
{
	namespace Settings
	{
		class GdiStretchBltMode : public MappedSetting<UINT>
		{
		public:
			static const UINT APP = 0;

			GdiStretchBltMode() : MappedSetting("GdiStretchBltMode", "app", {
				{"app", APP},
				{"coloroncolor", COLORONCOLOR},
				{"halftone", HALFTONE}
				})
			{
			}
		};
	}

	extern Settings::GdiStretchBltMode gdiStretchBltMode;
}
