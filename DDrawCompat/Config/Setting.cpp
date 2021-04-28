#include <Config/Parser.h>
#include <Config/Setting.h>

namespace Config
{
	Setting::Setting(const std::string& name)
		: m_name(name)
	{
		Parser::registerSetting(*this);
	}

	void Setting::reset()
	{
		resetValue();
		m_source = "default";
	}

	void Setting::set(const std::string& value, const std::string& source)
	{
		if ("default" == value)
		{
			resetValue();
		}
		else
		{
			setValue(value);
		}
		m_source = source;
	}
}
