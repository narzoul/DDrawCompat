#pragma once

#include <Config/EnumSetting.h>

namespace Config
{
	namespace Settings
	{
		class BltFilter : public EnumSetting
		{
		public:
			enum Values { POINT, BILINEAR };

			BltFilter()
				: EnumSetting("BltFilter", "point", { "point", "bilinear" })
			{
			}
		};
	}

	extern Settings::BltFilter bltFilter;
}
