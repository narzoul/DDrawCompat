#pragma once

#include <Config/EnumSetting.h>

namespace Config
{
	namespace Settings
	{
		class ResolutionScaleFilter : public EnumSetting
		{
		public:
			static const UINT POINT = 0;
			static const UINT BILINEAR = 1;

			ResolutionScaleFilter()
				: EnumSetting("ResolutionScaleFilter", "point", { "point", "bilinear" })
			{
			}
		};
	}

	extern Settings::ResolutionScaleFilter resolutionScaleFilter;
}
