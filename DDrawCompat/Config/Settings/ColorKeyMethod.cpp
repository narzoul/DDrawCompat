#include <Config/Settings/ColorKeyMethod.h>

namespace Config
{
	namespace Settings
	{
		ColorKeyMethod::ColorKeyMethod()
			: MappedSetting("ColorKeyMethod", "native", {
				{"none", NONE},
				{"native", NATIVE},
				{"alphatest", ALPHATEST}
				})
		{
		}

		Setting::ParamInfo ColorKeyMethod::getParamInfo() const
		{
			if (ALPHATEST == m_value)
			{
				return { "AlphaRef", 1, 255, 1, m_param };
			}
			return {};
		}
	}
}
