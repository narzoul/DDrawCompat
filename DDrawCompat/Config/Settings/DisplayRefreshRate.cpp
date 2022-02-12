#include <Config/Settings/DisplayRefreshRate.h>

namespace Config
{
	namespace Settings
	{
		DisplayRefreshRate::DisplayRefreshRate()
			: MappedSetting("DisplayRefreshRate", "app", { {"app", APP}, {"desktop", DESKTOP} })
		{
		}

		std::string DisplayRefreshRate::getValueStr() const
		{
			try
			{
				return MappedSetting::getValueStr();
			}
			catch (const ParsingError&)
			{
				return std::to_string(m_value);
			}
		}

		void DisplayRefreshRate::setValue(const std::string& value)
		{
			try
			{
				MappedSetting::setValue(value);
			}
			catch (const ParsingError&)
			{
				m_value = Parser::parseInt(value, 1, MAXINT);
			}
		}
	}
}
