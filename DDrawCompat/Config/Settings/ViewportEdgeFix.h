#pragma once

#include <Config/EnumSetting.h>

namespace Config
{
	namespace Settings
	{
		class ViewportEdgeFix : public EnumSetting
		{
		public:
			enum Values { OFF, SCALE };

			ViewportEdgeFix()
				: EnumSetting("ViewportEdgeFix", "off", { "off", "scale" })
			{
			}

			virtual ParamInfo getParamInfo() const override
			{
				switch (m_value)
				{
				case SCALE:
					return { "Gap", 1, 100, 50 };
				}
				return {};
			}
		};
	}

	extern Settings::ViewportEdgeFix viewportEdgeFix;
}
