#pragma once

#include <vector>

#include <Config/Setting.h>

namespace Config
{
	class EnumSetting : public Setting
	{
	public:
		unsigned get() const { return m_value; }

	protected:
		EnumSetting(const std::string& name, unsigned default, const std::vector<std::string>& enumNames);

	private:
		std::string getValueStr() const override;
		void resetValue() override;
		void setValue(const std::string& value) override;

		unsigned m_default;
		unsigned m_value;
		const std::vector<std::string> m_enumNames;
	};
}
