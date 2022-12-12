#pragma once

#include <Config/MappedSetting.h>

namespace Config
{
	namespace Settings
	{
		class ResolutionScaleFilter : public MappedSetting<UINT>
		{
		public:
			static const UINT POINT = 0;
			static const UINT BILINEAR = 1;

			ResolutionScaleFilter::ResolutionScaleFilter()
				: MappedSetting("ResolutionScaleFilter", "point", { {"point", POINT}, {"bilinear", BILINEAR} })
			{
			}
		};
	}

	extern Settings::ResolutionScaleFilter resolutionScaleFilter;
}
