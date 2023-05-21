#pragma once

#include <Config/EnumSetting.h>

namespace Config
{
	namespace Settings
	{
		class DisplayFilter : public EnumSetting
		{
		public:
			enum Values { POINT, BILINEAR, BICUBIC, LANCZOS, SPLINE };

			DisplayFilter();

			virtual ParamInfo getParamInfo() const override;
		};
	}

	extern Settings::DisplayFilter displayFilter;
}
