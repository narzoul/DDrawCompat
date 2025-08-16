#include <Config/Settings/DisplayFilter.h>

namespace Config
{
	namespace Settings
	{
		DisplayFilter::DisplayFilter()
			: EnumSetting("DisplayFilter", "bilinear", { "point", "integer", "bilinear", "bicubic", "lanczos", "spline" })
		{
		}

		Setting::ParamInfo DisplayFilter::getParamInfo() const
		{
			switch (m_value)
			{
			case BILINEAR:
			case BICUBIC:
				return { "Blur", 0, 100, 0 };
			case LANCZOS:
			case SPLINE:
				return { "Lobes", 2, 4, 2 };
			default:
				return {};
			}
		}
	}
}
