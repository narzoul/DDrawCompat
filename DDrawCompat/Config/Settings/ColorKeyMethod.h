#pragma once

#include <Config/MappedSetting.h>

namespace Config
{
	namespace Settings
	{
		class ColorKeyMethod : public MappedSetting<UINT>
		{
		public:
			static const UINT NONE = 0;
			static const UINT NATIVE = 1;
			static const UINT ALPHATEST = 2;

			ColorKeyMethod();

			virtual ParamInfo getParamInfo() const override;
		};
	}

	extern Settings::ColorKeyMethod colorKeyMethod;
}
