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

		std::string CpuAffinity::addValue(const std::string& value)
		{
			if ("app" == value)
			{
				if (0 != m_value)
				{
					throw ParsingError("'app' cannot be combined with other values");
				}
				return value;
			}

			if ("all" == value)
			{
				m_value = UINT_MAX;
				return value;
			}

			auto num = Parser::parseInt(value, 1, 32);
			m_value |= 1U << (num - 1);
			return std::to_string(num);
		}

		void CpuAffinity::clear()
		{
			m_value = 0;
		}
	}
}
