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
		MappedSetting(const std::string& name, Value default, const std::map<std::string, Value>& valueMapping)
			: Setting(name)
			, m_default(default)
			, m_value(default)
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

		void resetValue() override
		{
			m_value = m_default;
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

		Value m_default;
		Value m_value;
		const std::map<std::string, Value> m_valueMapping;
	};
}
