#include <Config/Settings/DisplayResolution.h>

namespace Config
{
	namespace Settings
	{
		DisplayResolution::DisplayResolution()
			: MappedSetting("DisplayResolution", DESKTOP, { {"app", APP}, {"desktop", DESKTOP} })
		{
		}

		std::string DisplayResolution::getValueStr() const
		{
			try
			{
				return MappedSetting::getValueStr();
			}
			catch (const ParsingError&)
			{
				return std::to_string(m_value.cx) + 'x' + std::to_string(m_value.cy);
			}
		}

		void DisplayResolution::setValue(const std::string& value)
		{
			try
			{
				MappedSetting::setValue(value);
			}
			catch (const ParsingError&)
			{
				m_value = Parser::parseResolution(value);
			}
		}

		const SIZE DisplayResolution::APP = { 0, 0 };
		const SIZE DisplayResolution::DESKTOP = { 1, 0 };
	}
}
