#pragma once

#include <map>

#include <Config/Parser.h>
#include <Config/Setting.h>

namespace Config
{
	template <typename Value>
	class MappedSetting : public Setting
	{
	public:
		Value get() const { return m_value; }

	protected:
		MappedSetting(const std::string& name, const std::string& default, const std::map<std::string, Value>& valueMapping)
			: Setting(name, default)
			, m_value{}
			, m_valueMapping(valueMapping)
		{
		}

		std::string getValueStr() const override
		{
			for (const auto& pair : m_valueMapping)
			{
				if (pair.second == m_value)
				{
					return pair.first;
				}
			}
			throw ParsingError("MappedSetting::getValueStr(): value not found in mapping");
		}

		void setValue(const std::string& value) override
		{
			auto it = m_valueMapping.find(value);
			if (it == m_valueMapping.end())
			{
				throw ParsingError("invalid value: '" + value + "'");
			}
			m_value = it->second;
		}

		Value m_value;
		const std::map<std::string, Value> m_valueMapping;
	};
}
