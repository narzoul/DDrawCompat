#include <algorithm>

#include <Config/Parser.h>
#include <Config/EnumSetting.h>

namespace Config
{
	EnumSetting::EnumSetting(const std::string& name, unsigned default, const std::vector<std::string>& enumNames)
		: Setting(name)
		, m_default(default)
		, m_value(default)
		, m_enumNames(enumNames)
	{
	}

	std::string EnumSetting::getValueStr() const
	{
		return m_enumNames[m_value];
	}

	void EnumSetting::resetValue()
	{
		m_value = m_default;
	}

	void EnumSetting::setValue(const std::string& value)
	{
		auto it = std::find(m_enumNames.begin(), m_enumNames.end(), value);
		if (it == m_enumNames.end())
		{
			throw ParsingError("invalid value: '" + value + "'");
		}
		m_value = it - m_enumNames.begin();
	}
}
