#include <Config/Settings/DesktopResolution.h>

namespace Config
{
	namespace Settings
	{
		DesktopResolution::DesktopResolution()
			: MappedSetting("DesktopResolution", "desktop", { {"desktop", DESKTOP}, {"initial", INITIAL} })
		{
		}

		std::string DesktopResolution::getValueStr() const
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

		void DesktopResolution::setValue(const std::string& value)
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

		const SIZE DesktopResolution::DESKTOP = { 0, 0 };
		const SIZE DesktopResolution::INITIAL = { 1, 0 };
	}
}
