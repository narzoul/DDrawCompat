#pragma once

#include <Config/EnumSetting.h>

namespace Config
{
	namespace Settings
	{
		class AltTabFix : public EnumSetting
		{
		public:
			enum Values { OFF, KEEPVIDMEM, NOACTIVATEAPP };

			AltTabFix()
				: EnumSetting("AltTabFix", "off", { "off", "keepvidmem", "noactivateapp" })
			{
			}

			virtual ParamInfo getParamInfo() const override
			{
				switch (m_value)
				{
				case KEEPVIDMEM:
					return { "KeepPrimary", 0, 1, 1 };
				case NOACTIVATEAPP:
					return { "PassToApp", 0, 1, 1 };
				}
				return {};
			}
		};
	}

	extern Settings::AltTabFix altTabFix;
}
