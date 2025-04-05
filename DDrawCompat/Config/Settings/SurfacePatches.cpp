#include <Common/Comparison.h>
#include <Config/Parser.h>
#include <Config/Settings/SurfacePatches.h>

namespace
{
	unsigned calcExtraRows(unsigned height, int adjustment)
	{
		return adjustment < 0 ? (height * -adjustment + 99) / 100 : adjustment;
	}
}

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
			if (std::string::npos == separator || 0 == separator)
			{
				throw ParsingError("invalid value: '" + value + "'");
			}

			auto specifier = value.substr(0, separator);
			auto number = value.substr(separator + 1);
			auto isPercent = !number.empty() && '%' == number.back();
			if (isPercent)
			{
				number.pop_back();
			}
			auto rows = Parser::parseInt(number, 1, 1000);
			if ("top" == specifier)
			{
				m_top = isPercent ? -rows : rows;
			}
			else if ("bottom" == specifier)
			{
				m_bottom = isPercent ? -rows : rows;
			}
			else
			{
				throw ParsingError("invalid specifier: '" + specifier + "'");
			}
			return specifier + ':' + std::to_string(rows) + (isPercent ? "%" : "");
		}

		void SurfacePatches::clear()
		{
			m_top = 0;
			m_bottom = 0;
		}

		unsigned SurfacePatches::getExtraRows(unsigned height)
		{
			return calcExtraRows(height, m_top) + calcExtraRows(height, m_bottom);
		}

		unsigned SurfacePatches::getTopRows(unsigned height)
		{
			return calcExtraRows(height, m_top);
		}
	}
}
