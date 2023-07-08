#pragma once

#include <string>
#include <vector>

#include <Config/ListSetting.h>

namespace Config
{
	namespace Settings
	{
		class ConfigRows : public ListSetting
		{
		public:
			ConfigRows();

			virtual std::string getValueStr() const override;

			const std::vector<Setting*>& get() const { return m_settings; }

		private:
			void setValues(const std::vector<std::string>& values) override;

			std::vector<Setting*> m_settings;
			std::string m_valueStr;
		};
	}

	extern Settings::ConfigRows configRows;
}
