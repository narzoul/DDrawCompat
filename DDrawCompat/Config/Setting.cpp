#include <Config/Parser.h>
#include <Config/Setting.h>

namespace Config
{
	Setting::Setting(const std::string& name, const std::string& default)
		: m_name(name)
		, m_default(default)
	{
		Parser::registerSetting(*this);
	}

	void Setting::reset()
	{
		set("default", "default");
	}

	void Setting::set(const std::string& value, const std::string& source)
	{
		setValue("default" == value ? m_default : value);
		m_source = source;
	}
}
