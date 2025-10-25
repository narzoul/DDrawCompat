#include <Config/EnumListSetting.h>
#include <Config/Parser.h>

namespace Config
{
	EnumListSetting::EnumListSetting(const std::string& name, const std::string& defaultValue,
		const std::vector<std::string>& enumNames)
		: ListSetting(name, defaultValue)
		, m_enumNames(enumNames)
	{
	}

	std::string EnumListSetting::addValue(const std::string& value)
	{
		const auto it = std::find(m_enumNames.begin(), m_enumNames.end(), value);
		if (it == m_enumNames.end())
		{
			throw ParsingError("invalid value: '" + value + "'");
		}

		const int index = it - m_enumNames.begin();
		if (std::find(m_values.begin(), m_values.end(), index) == m_values.end())
		{
			m_values.push_back(index);
		}
		return value;
	}

	void EnumListSetting::clear()
	{
		m_values.clear();
	}
}
