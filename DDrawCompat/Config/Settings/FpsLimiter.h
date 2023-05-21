#pragma once

#include <Config/EnumSetting.h>

namespace Config
{
	namespace Settings
	{
		class FpsLimiter : public EnumSetting
		{
		public:
			enum Values { OFF, FLIPSTART, FLIPEND, MSGLOOP };

			FpsLimiter();

			virtual ParamInfo getParamInfo() const override;
		};
	}

	extern Settings::FpsLimiter fpsLimiter;
}
