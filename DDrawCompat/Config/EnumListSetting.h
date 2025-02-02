#pragma once

#include <vector>

#include <Config/ListSetting.h>

namespace Config
{
	class EnumListSetting : public ListSetting
	{
	public:
		EnumListSetting(const std::string& name, const std::string& default, const std::vector<std::string>& enumNames);

		const std::vector<int>& get() const { return m_values; }

	private:
		std::string addValue(const std::string& value) override;
		void clear() override;

		const std::vector<std::string> m_enumNames;
		std::vector<int> m_values;
	};
}
