#include <d3dtypes.h>
#include <d3dumddi.h>

#include <Config/Settings/VSync.h>

namespace Config
{
	namespace Settings
	{
		VSync::VSync()
			: MappedSetting("VSync", "app", { {"app", APP}, {"off", OFF}, {"on", ON} })
		{
		}

		Setting::ParamInfo VSync::getParamInfo() const
		{
			if (ON == m_value)
			{
				return { "Interval", 1, 16, 1, m_param };
			}
			return {};
		}
	}
}
