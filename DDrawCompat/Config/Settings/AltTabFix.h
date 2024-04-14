#pragma once

#include <Config/EnumSetting.h>

namespace Config
{
	namespace Settings
	{
		class AltTabFix : public EnumSetting
		{
		public:
			enum Values { OFF, KEEPVIDMEM };

			AltTabFix()
				: EnumSetting("AltTabFix", "off", { "off", "keepvidmem" })
			{
			}

			virtual ParamInfo getParamInfo() const override
			{
				return KEEPVIDMEM == m_value ? ParamInfo{ "KeepPrimary", 0, 1, 1 } : ParamInfo{};
			}
		};
	}

	extern Settings::AltTabFix altTabFix;
}
