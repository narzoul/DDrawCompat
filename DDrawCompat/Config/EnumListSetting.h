#pragma once

#include <vector>

#include <Config/ListSetting.h>

namespace Config
{
	class EnumListSetting : public ListSetting
	{
	public:
		EnumListSetting(const std::string& name, const std::string& default, const std::vector<std::string>& enumNames);

		virtual std::string getValueStr() const override;

		const std::vector<unsigned>& get() const { return m_values; }

	private:
		void setValues(const std::vector<std::string>& values) override;

		const std::vector<std::string> m_enumNames;
		std::vector<unsigned> m_values;
	};
}
