#pragma once

#include <Config/MappedSetting.h>

namespace Config
{
	namespace Settings
	{
		class BltFilter : public MappedSetting<UINT>
		{
		public:
			static const UINT POINT = 1;
			static const UINT BILINEAR = 2;

			BltFilter::BltFilter()
				: MappedSetting("BltFilter", "point", { {"point", POINT}, {"bilinear", BILINEAR} })
			{
			}
		};
	}

	extern Settings::BltFilter bltFilter;
}
