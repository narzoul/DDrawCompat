#pragma once

#include <Config/MappedSetting.h>

namespace Config
{
	namespace Settings
	{
		class FpsLimiter : public MappedSetting<UINT>
		{
		public:
			static const UINT OFF = 0;
			static const UINT FLIPSTART = 1;
			static const UINT FLIPEND = 2;
			static const UINT MSGLOOP = 3;

			FpsLimiter();

			virtual ParamInfo getParamInfo() const override;
		};
	}
}
