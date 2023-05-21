#pragma once

#include <Config/EnumSetting.h>

namespace Config
{
	namespace Settings
	{
		class ColorKeyMethod : public EnumSetting
		{
		public:
			enum Values { NONE, NATIVE, ALPHATEST };

			ColorKeyMethod();

			virtual ParamInfo getParamInfo() const override;
		};
	}

	extern Settings::ColorKeyMethod colorKeyMethod;
}
