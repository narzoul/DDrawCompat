#include <Config/Parser.h>
#include <Config/Setting.h>

namespace Config
{
	Setting::Setting(const std::string& name, const std::string& default)
		: m_param(0)
		, m_name(name)
		, m_default(default)
	{
		Parser::registerSetting(*this);
	}

	void Setting::reset()
	{
		set("default", "default");
	}

	void Setting::set(const std::string& value)
	{
		if ("default" == value)
		{
			set(m_default);
			return;
		}

		std::string val(value);
		std::string param;
		auto parenPos = value.find('(');
		if (std::string::npos != parenPos)
		{
			val = Parser::trim(value.substr(0, parenPos));
			param = value.substr(parenPos + 1);
			if (param.back() != ')')
			{
				throw ParsingError("invalid value: '" + value + "'");
			}
			param = Parser::trim(param.substr(0, param.length() - 1));
		}

		setValue(val);

		try
		{
			m_param = getParamInfo().default;
			if (!param.empty())
			{
				setParam(param);
			}
		}
		catch (const ParsingError& e)
		{
			throw ParsingError(Parser::removeParam(getValueStr()) + ": " + e.what());
		}
	}

	void Setting::set(const std::string& value, const std::string& source)
	{
		set(value);
		m_source = source;
	}

	void Setting::setBaseValue()
	{
		m_baseValue = getValueStr();
	}

	void Setting::setExportedValue()
	{
		m_exportedValue = getValueStr();
	}

	void Setting::setParam(const std::string& param)
	{
		const auto paramInfo = getParamInfo();
		if (paramInfo.name.empty())
		{
			throw ParsingError("parameters are not allowed");
		}

		try
		{
			m_param = Parser::parseInt(param, paramInfo.min, paramInfo.max);
		}
		catch (const ParsingError&)
		{
			throw ParsingError("invalid parameter value: '" + param + "'");
		}
	}
}
