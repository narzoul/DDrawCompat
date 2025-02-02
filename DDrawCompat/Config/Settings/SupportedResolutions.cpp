#include <Common/Comparison.h>
#include <Config/Parser.h>
#include <Config/Settings/SupportedResolutions.h>

namespace Config
{
	namespace Settings
	{
		SupportedResolutions::SupportedResolutions()
			: ListSetting("SupportedResolutions", "native, 640x480, 800x600, 1024x768")
		{
		}

		std::string SupportedResolutions::addValue(const std::string& value)
		{
			if ("native" == value)
			{
				m_resolutions.insert(NATIVE);
				return value;
			}

			const SIZE resolution = Parser::parseResolution(value);
			m_resolutions.insert(resolution);
			return std::to_string(resolution.cx) + 'x' + std::to_string(resolution.cy);
		}

		void SupportedResolutions::clear()
		{
			m_resolutions.clear();
		}

		const SIZE SupportedResolutions::NATIVE = { 0, 0 };
	}
}
