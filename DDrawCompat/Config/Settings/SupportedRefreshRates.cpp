#include <Config/Parser.h>
#include <Config/Settings/SupportedRefreshRates.h>

namespace Config
{
	namespace Settings
	{
		SupportedRefreshRates::SupportedRefreshRates()
			: ListSetting("SupportedRefreshRates", "native")
			, m_allowAll(false)
		{
		}

		std::string SupportedRefreshRates::addValue(const std::string& value)
		{
			if ("native" == value)
			{
				m_refreshRates.clear();
				m_allowAll = true;
				return value;
			}

			const auto refreshRate = "desktop" == value ? DESKTOP : Parser::parseInt(value, 1, INT_MAX);
			if (!m_allowAll)
			{
				m_refreshRates.insert(refreshRate);
			}
			return "desktop" == value ? value : std::to_string(refreshRate);
		}

		void SupportedRefreshRates::clear()
		{
			m_refreshRates.clear();
			m_allowAll = false;
		}
	}
}
