#include <Common/Comparison.h>
#include <Config/Parser.h>
#include <Config/Settings/SurfacePatches.h>

namespace Config
{
	namespace Settings
	{
		SurfacePatches::SurfacePatches()
			: ListSetting("SurfacePatches", "none")
			, m_top(0)
			, m_bottom(0)
		{
		}

		std::string SurfacePatches::addValue(const std::string& value)
		{
			auto separator = value.find(':');
			if (std::string::npos == separator || 0 == separator || value.size() - 1 == separator)
			{
				throw ParsingError("invalid value: '" + value + "'");
			}

			auto specifier = value.substr(0, separator);
			auto rows = Parser::parseInt(value.substr(separator + 1), 1, 1000);
			if ("top" == specifier)
			{
				m_top = rows;
			}
			else if ("bottom" == specifier)
			{
				m_bottom = rows;
			}
			else
			{
				throw ParsingError("invalid specifier: '" + specifier + "'");
			}
			return specifier + ':' + std::to_string(rows);
		}

		void SurfacePatches::clear()
		{

		}
	}
}
