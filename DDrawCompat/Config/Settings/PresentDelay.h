#pragma once

#include <Config/BoolSetting.h>

namespace Config
{
	namespace Settings
	{
		class PresentDelay : public BoolSetting
		{
		public:
			PresentDelay() : BoolSetting("PresentDelay", "on")
			{
			}

			virtual ParamInfo getParamInfo() const override
			{
				if (m_value)
				{
					return { "MaxDelay", 1, 50, 10 };
				}
				return {};
			}
		};
	}

	extern Settings::PresentDelay presentDelay;
}
