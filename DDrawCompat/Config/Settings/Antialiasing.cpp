#include <d3dtypes.h>
#include <d3dumddi.h>

#include <Config/Settings/Antialiasing.h>

namespace Config
{
	namespace Settings
	{
		Antialiasing::Antialiasing()
			: MappedSetting("Antialiasing", "off", {
				{"off", D3DDDIMULTISAMPLE_NONE},
				{"msaa", D3DDDIMULTISAMPLE_NONMASKABLE},
				{"msaa2x", D3DDDIMULTISAMPLE_2_SAMPLES},
				{"msaa4x", D3DDDIMULTISAMPLE_4_SAMPLES},
				{"msaa8x", D3DDDIMULTISAMPLE_8_SAMPLES}
				})
		{
		}

		Setting::ParamInfo Antialiasing::getParamInfo() const
		{
			if (D3DDDIMULTISAMPLE_NONE != m_value)
			{
				return { "Quality", 0, 7, 7, m_param };
			}
			return {};
		}
	}
}
