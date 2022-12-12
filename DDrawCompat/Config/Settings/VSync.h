#pragma once

#include <Config/MappedSetting.h>

namespace Config
{
	namespace Settings
	{
		class VSync : public MappedSetting<int>
		{
		public:
			static const int APP = -1;
			static const int OFF = 0;
			static const int ON = 1;

			VSync();

			virtual ParamInfo getParamInfo() const override;
		};
	}

	extern Settings::VSync vSync;
}
