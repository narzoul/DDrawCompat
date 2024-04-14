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

		std::string SurfacePatches::getValueStr() const
		{
			std::string result;
			if (0 != m_top)
			{
				appendToList(result, "top:" + std::to_string(m_top));
			}
			if (0 != m_bottom)
			{
				appendToList(result, "bottom:" + std::to_string(m_bottom));
			}
			if (result.empty())
			{
				result = "none";
			}
			return result;
		}

		void SurfacePatches::setValues(const std::vector<std::string>& values)
		{
			unsigned top = 0;
			unsigned bottom = 0;

			for (const auto& v : values)
			{
				if ("none" == v)
				{
					if (1 != values.size())
					{
						throw ParsingError("'none' cannot be combined with other values");
					}
					m_top = 0;
					m_bottom = 0;
					return;
				}

				auto separator = v.find(':');
				if (std::string::npos == separator || 0 == separator || v.size() - 1 == separator)
				{
					throw ParsingError("invalid value: '" + v + "'");
				}

				auto specifier = v.substr(0, separator);
				auto rows = Parser::parseInt(v.substr(separator + 1), 1, 1000);
				if ("top" == specifier)
				{
					top = rows;
				}
				else if ("bottom" == specifier)
				{
					bottom = rows;
				}
				else
				{
					throw ParsingError("invalid specifier: '" + specifier + "'");
				}
			}

			m_top = top;
			m_bottom = bottom;
		}
	}
}
