#pragma once

#include <Config/EnumSetting.h>

namespace Config
{
	namespace Settings
	{
		class FullscreenMode : public EnumSetting
		{
		public:
			static const UINT BORDERLESS = 0;
			static const UINT EXCLUSIVE = 1;

			FullscreenMode()
				: EnumSetting("FullscreenMode", "borderless", { "borderless", "exclusive" })
			{
			}

			virtual ParamInfo getParamInfo() const override
			{
				if (EXCLUSIVE == m_value)
				{
					return { "VSync", 0, 1, 1 };
				}
				return {};
			}
		};
	}

	extern Settings::FullscreenMode fullscreenMode;
}
