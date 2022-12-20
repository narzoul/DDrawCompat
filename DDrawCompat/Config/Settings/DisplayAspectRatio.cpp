#include <Config/Settings/DisplayAspectRatio.h>

namespace Config
{
	namespace Settings
	{
		DisplayAspectRatio::DisplayAspectRatio()
			: MappedSetting("DisplayAspectRatio", "app", { {"app", APP}, {"display", DISPLAY} })
		{
		}

		std::string DisplayAspectRatio::getValueStr() const
		{
			try
			{
				return MappedSetting::getValueStr();
			}
			catch (const ParsingError&)
			{
				return std::to_string(m_value.cx) + ':' + std::to_string(m_value.cy);
			}
		}

		void DisplayAspectRatio::setValue(const std::string& value)
		{
			try
			{
				MappedSetting::setValue(value);
			}
			catch (const ParsingError&)
			{
				m_value = Parser::parseAspectRatio(value);
			}
		}

		const SIZE DisplayAspectRatio::APP = { 0, 0 };
		const SIZE DisplayAspectRatio::DISPLAY = { 1, 0 };
	}
}
