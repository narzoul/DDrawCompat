#pragma once

#include <Config/EnumSetting.h>

namespace Config
{
	namespace Settings
	{
		class MouseSensitivity : public EnumSetting
		{
		public:
			enum Values { NATIVE, DESKTOP, NOACCEL };

			MouseSensitivity()
				: EnumSetting("MouseSensitivity", "desktop", { "native", "desktop", "noaccel" })
			{
			}

			virtual ParamInfo getParamInfo() const override
			{
				switch (m_value)
				{
				case DESKTOP:
				case NOACCEL:
					return { "Scale", 10, 400, 100 };
				}
				return {};
			}
		};
	}

	extern Settings::MouseSensitivity mouseSensitivity;
}
