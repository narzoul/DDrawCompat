#include <d3dtypes.h>
#include <d3dumddi.h>

#include <Config/Settings/VSync.h>

namespace Config
{
	namespace Settings
	{
		VSync::VSync()
			: EnumSetting("VSync", "app", { "app", "off", "on", "wait"})
		{
		}

		Setting::ParamInfo VSync::getParamInfo() const
		{
			if (ON == m_value || WAIT == m_value)
			{
				return { "Interval", 1, 16, 1 };
			}
			return {};
		}
	}
}
