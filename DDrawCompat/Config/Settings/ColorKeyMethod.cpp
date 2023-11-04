#include <Config/Settings/ColorKeyMethod.h>

namespace Config
{
	namespace Settings
	{
		ColorKeyMethod::ColorKeyMethod()
			: EnumSetting("ColorKeyMethod", "auto", { "auto", "none", "native", "alphatest" })
		{
		}

		Setting::ParamInfo ColorKeyMethod::getParamInfo() const
		{
			if (ALPHATEST == m_value)
			{
				return { "AlphaRef", 1, 255, 1 };
			}
			return {};
		}
	}
}
