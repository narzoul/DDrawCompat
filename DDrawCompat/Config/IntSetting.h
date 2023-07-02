#pragma once

#include <vector>

#include <Config/Parser.h>
#include <Config/Setting.h>

namespace Config
{
	class IntSetting : public Setting
	{
	public:
		int get() const { return m_value; }

		virtual std::string getValueStr() const override
		{
			return std::to_string(m_value);
		}

	protected:
		IntSetting(const std::string& name, const std::string& default, int min, int max)
			: Setting(name, default)
			, m_min(min)
			, m_max(max)
			, m_value(min)
		{
		}

		virtual void setValue(const std::string& value) override
		{
			m_value = Parser::parseInt(value, m_min, m_max);
		}

	private:
		int m_min;
		int m_max;
		int m_value;
	};
}
