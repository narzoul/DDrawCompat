#include <Config/Settings/DisplayFilter.h>

namespace Config
{
	namespace Settings
	{
		DisplayFilter::DisplayFilter()
			: MappedSetting("DisplayFilter", "bilinear", {
				{"point", POINT},
				{"bilinear", BILINEAR},
				{"lanczos", LANCZOS},
				})
		{
		}

		Setting::ParamInfo DisplayFilter::getParamInfo() const
		{
			switch (m_value)
			{
			case BILINEAR:
				return { "Blur", 0, 100, 0, m_param };
			case LANCZOS:
				return { "Lobes", 2, 4, 2, m_param };
			default:
				return {};
			}
		}
	}
}
