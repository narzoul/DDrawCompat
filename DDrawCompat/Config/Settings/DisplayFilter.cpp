#include <Config/Settings/DisplayFilter.h>

namespace Config
{
	namespace Settings
	{
		DisplayFilter::DisplayFilter()
			: MappedSetting("DisplayFilter", "bilinear", { {"point", POINT}, {"bilinear", BILINEAR} })
		{
		}

		Setting::ParamInfo DisplayFilter::getParamInfo() const
		{
			if (BILINEAR == m_value)
			{
				return { "Blur", 0, 100, 0, m_param };
			}
			return {};
		}
	}
}
