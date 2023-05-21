#include <d3dtypes.h>
#include <d3dumddi.h>

#include <Config/Settings/VSync.h>

namespace Config
{
	namespace Settings
	{
		VSync::VSync()
			: EnumSetting("VSync", "app", { "app", "off", "on" })
		{
		}

		Setting::ParamInfo VSync::getParamInfo() const
		{
			if (ON == m_value)
			{
				return { "Interval", 1, 16, 1 };
			}
			return {};
		}
	}
}
