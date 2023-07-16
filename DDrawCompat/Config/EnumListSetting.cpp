#include <Config/EnumListSetting.h>
#include <Config/Parser.h>

namespace Config
{
	EnumListSetting::EnumListSetting(const std::string& name, const std::string& default,
		const std::vector<std::string>& enumNames)
		: ListSetting(name, default)
		, m_enumNames(enumNames)
	{
	}

	std::string EnumListSetting::getValueStr() const
	{
		std::string result;
		for (auto value : m_values)
		{
			result += ", " + m_enumNames[value];
		}
		return result.substr(2);
	}

	void EnumListSetting::setValues(const std::vector<std::string>& values)
	{
		if (values.empty())
		{
			throw ParsingError("empty list is not allowed");
		}

		std::vector<int> result;
		for (auto valueName : values)
		{
			auto it = std::find(m_enumNames.begin(), m_enumNames.end(), valueName);
			if (it == m_enumNames.end())
			{
				throw ParsingError("invalid value: '" + valueName + "'");
			}

			int value = it - m_enumNames.begin();
			if (std::find(result.begin(), result.end(), value) == result.end())
			{
				result.push_back(value);
			}
		}
		m_values = result;
	}
}
