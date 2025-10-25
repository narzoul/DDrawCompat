#pragma once

#include <algorithm>
#include <vector>

#include <Config/Parser.h>
#include <Config/Setting.h>

namespace Config
{
	template <typename Value>
	class MappedSetting : public Setting
	{
	public:
		Value get() const { return m_value; }

		virtual std::vector<std::string> getDefaultValueStrings() override
		{
			if (m_defaultValueStrings.empty())
			{
				auto prevValue = m_value;
				auto prevParam = m_param;
				for (const auto& pair : m_valueMapping)
				{
					m_value = pair.second;
					m_param = getParamInfo().defaultValue;
					m_defaultValueStrings.push_back(getValueStr());
				}
				m_value = prevValue;
				m_param = prevParam;

			}
			return m_defaultValueStrings;
		}

		virtual std::string getValueStr() const override
		{
			for (const auto& pair : m_valueMapping)
			{
				if (pair.second == m_value)
				{
					const auto paramInfo = getParamInfo();
					if (!paramInfo.name.empty())
					{
						return pair.first + '(' + std::to_string(m_param) + ')';
					}
					return pair.first;
				}
			}
			throw ParsingError("MappedSetting::getValueStr(): value not found in mapping");
		}

		std::string convertToString(Value value)
		{
			for (const auto& pair : m_valueMapping)
			{
				if (pair.second == value)
				{
					return pair.first;
				}
			}
			return {};
		}

	protected:
		MappedSetting(const std::string& name, const std::string& defaultValue,
			const std::vector<std::pair<std::string, Value>>& valueMapping)
			: Setting(name, defaultValue)
			, m_value{}
			, m_valueMapping(valueMapping)
		{
		}

		virtual void setValue(const std::string& value) override
		{
			auto it = std::find_if(m_valueMapping.begin(), m_valueMapping.end(),
				[&](const auto& v) { return v.first == value; });
			if (it == m_valueMapping.end())
			{
				throw ParsingError("invalid value: '" + value + "'");
			}
			m_value = it->second;
		}

		Value m_value;
		const std::vector<std::pair<std::string, Value>> m_valueMapping;
		std::vector<std::string> m_defaultValueStrings;
	};
}
