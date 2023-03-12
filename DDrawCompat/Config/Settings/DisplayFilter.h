#pragma once

#include <Config/MappedSetting.h>

namespace Config
{
	namespace Settings
	{
		class DisplayFilter : public MappedSetting<UINT>
		{
		public:
			static const UINT POINT = 0;
			static const UINT BILINEAR = 1;
			static const UINT BICUBIC = 2;
			static const UINT LANCZOS = 3;
			static const UINT SPLINE = 4;

			DisplayFilter();

			virtual ParamInfo getParamInfo() const override;
		};
	}

	extern Settings::DisplayFilter displayFilter;
}
