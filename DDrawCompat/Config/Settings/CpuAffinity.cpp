#include <Config/Parser.h>
#include <Config/Settings/CpuAffinity.h>

namespace Config
{
	namespace Settings
	{
		CpuAffinity::CpuAffinity()
			: ListSetting("CpuAffinity", "1")
			, m_value(1)
		{
		}

		std::string CpuAffinity::getValueStr() const
		{
			if (0 == m_value)
			{
				return "app";
			}

			if (UINT_MAX == m_value)
			{
				return "all";
			}

			std::string result;
			for (unsigned i = 0; i < 32; ++i)
			{
				if (m_value & (1U << i))
				{
					result += ',' + std::to_string(i + 1);
				}
			}

			return result.substr(1);
		}

		void CpuAffinity::setValues(const std::vector<std::string>& values)
		{
			if (values.empty())
			{
				throw ParsingError("empty list is not allowed");
			}

			if (1 == values.size())
			{
				if ("app" == values.front())
				{
					m_value = 0;
					return;
				}
				else if ("all" == values.front())
				{
					m_value = UINT_MAX;
					return;
				}
			}

			unsigned result = 0;
			for (const auto& value : values)
			{
				auto num = Parser::parseUnsigned(value);
				if (num < 1 || num > 32)
				{
					throw ParsingError("'" + value + "' is not an integer between 1 and 32");
				}
				result |= 1U << (num - 1);
			}

			m_value = result;
		}
	}
}
