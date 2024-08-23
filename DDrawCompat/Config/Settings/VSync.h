#pragma once

#include <Config/EnumSetting.h>

namespace Config
{
	namespace Settings
	{
		class VSync : public EnumSetting
		{
		public:
			enum Values { APP, OFF, ON, WAIT };

			VSync();

			virtual ParamInfo getParamInfo() const override;
		};
	}

	extern Settings::VSync vSync;
}
