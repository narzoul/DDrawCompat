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

		virtual std::string getParamStr() const
		{
			return {};
		}

		virtual std::string getValueStr() const override
		{
			for (const auto& pair : m_valueMapping)
			{
				if (pair.second == m_value)
				{
					std::string param(getParamStr());
					if (!param.empty())
					{
						param = '(' + param + ')';
					}
					return pair.first + param;
				}
			}
			throw ParsingError("MappedSetting::getValueStr(): value not found in mapping");
		}

		virtual void setDefaultParam(const Value& /*value*/)
		{
		}

		virtual void setValue(const std::string& value) override
		{
			std::string val(value);
			std::string param;
			auto parenPos = value.find('(');
			if (std::string::npos != parenPos)
			{
				val = value.substr(0, parenPos);
				param = value.substr(parenPos + 1);
				if (param.length() < 2 || param.back() != ')')
				{
					throw ParsingError("invalid value: '" + value + "'");
				}
				param = param.substr(0, param.length() - 1);
			}

			auto it = m_valueMapping.find(val);
			if (it == m_valueMapping.end())
			{
				throw ParsingError("invalid value: '" + value + "'");
			}

			if (param.empty())
			{
				m_value = it->second;
				setDefaultParam(it->second);
			}
			else
			{
				setValue(it->second, param);
			}
		}

		virtual void setValue(const Value& /*value*/, const std::string& param)
		{
			throw ParsingError("invalid parameter: '" + param + "'");
		}

		Value m_value;
		const std::map<std::string, Value> m_valueMapping;
	};
}
