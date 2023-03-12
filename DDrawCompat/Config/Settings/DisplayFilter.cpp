#include <Config/Settings/DisplayFilter.h>

namespace Config
{
	namespace Settings
	{
		DisplayFilter::DisplayFilter()
			: MappedSetting("DisplayFilter", "bilinear", {
				{"point", POINT},
				{"bilinear", BILINEAR},
				{"bicubic", BICUBIC},
				{"lanczos", LANCZOS},
				{"spline", SPLINE}
				})
		{
		}

		Setting::ParamInfo DisplayFilter::getParamInfo() const
		{
			switch (m_value)
			{
			case BILINEAR:
			case BICUBIC:
				return { "Blur", 0, 100, 0, m_param };
			case LANCZOS:
			case SPLINE:
				return { "Lobes", 2, 4, 2, m_param };
			default:
				return {};
			}
		}
	}
}
