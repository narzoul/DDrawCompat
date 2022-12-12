#pragma once

#include <Config/MappedSetting.h>

namespace Config
{
	namespace Settings
	{
		class Antialiasing : public MappedSetting<UINT>
		{
		public:
			Antialiasing();
			
			virtual ParamInfo getParamInfo() const override;
		};
	}

	extern Settings::Antialiasing antialiasing;
}
