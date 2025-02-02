#include <Config/ListSetting.h>
#include <Config/Parser.h>

namespace Config
{
	ListSetting::ListSetting(const std::string& name, const std::string& default)
		: Setting(name, default)
	{
	}

	std::string ListSetting::getValueStr() const
	{
		if (m_valueStr.empty())
		{
			return "none";
		}
		return m_valueStr;
	}

	void ListSetting::setValue(const std::string& value)
	{
		m_valueStr.clear();
		clear();

		if ("none" == value)
		{
			return;
		}

		std::size_t startPos = 0;
		std::size_t endPos = 0;

		do
		{
			endPos = value.find(',', startPos);
			if (endPos == std::string::npos)
			{
				endPos = value.length();
			}

			auto val = Parser::trim(value.substr(startPos, endPos - startPos));
			if ("none" == val)
			{
				throw ParsingError("'none' has no effect when multiple values are specified");
			}

			try
			{
				val = addValue(val);
				if (!m_valueStr.empty())
				{
					m_valueStr += ", ";
				}
				m_valueStr += val;
			}
			catch (const ParsingError& e)
			{
				Parser::logError(e.what());
			}
			startPos = endPos + 1;
		} while (endPos < value.length());
	}
}
