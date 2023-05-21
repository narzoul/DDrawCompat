#include <Config/Settings/ResolutionScale.h>

namespace Config
{
	namespace Settings
	{
		ResolutionScale::ResolutionScale()
			: MappedSetting("ResolutionScale", "app", { {"app", APP}, {"display", DISPLAY} })
		{
		}

		std::string ResolutionScale::getValueStr() const
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

		Setting::ParamInfo ResolutionScale::getParamInfo() const
		{
			return { "Multiplier", APP == m_value ? 1 : -16, 16, 1 };
		}

		void ResolutionScale::setValue(const std::string& value)
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

		const SIZE ResolutionScale::APP = { 0, 0 };
		const SIZE ResolutionScale::DISPLAY = { 1, 0 };
	}
}
