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

			const std::vector<Setting*>& get() const { return m_settings; }

		private:
			virtual std::string addValue(const std::string& value) override;
			virtual void clear() override;

			std::vector<Setting*> m_settings;
		};
	}

	extern Settings::ConfigRows configRows;
}
