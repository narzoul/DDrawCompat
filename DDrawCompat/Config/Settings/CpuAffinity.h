#pragma once

#include <Config/ListSetting.h>

namespace Config
{
	namespace Settings
	{
		class CpuAffinity : public ListSetting
		{
		public:
			CpuAffinity();

			unsigned get() const { return m_value; }

		private:
			std::string getValueStr() const override;
			void setValues(const std::vector<std::string>& values) override;

			unsigned m_value;
		};
	}
}
