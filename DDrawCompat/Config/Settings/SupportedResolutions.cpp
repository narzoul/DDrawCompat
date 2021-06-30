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

		std::string SupportedResolutions::getValueStr() const
		{
			std::string result;
			for (const auto& res : m_resolutions)
			{
				result += ", ";
				result += NATIVE == res ? "native" : std::to_string(res.cx) + 'x' + std::to_string(res.cy);
			}
			return result.substr(2);
		}

		void SupportedResolutions::setValues(const std::vector<std::string>& values)
		{
			std::set<SIZE> result;
			for (const auto& v : values)
			{
				result.insert("native" == v ? NATIVE : Parser::parseResolution(v));
			}
			m_resolutions = result;
		}

		const SIZE SupportedResolutions::NATIVE = { 0, 0 };
	}
}
