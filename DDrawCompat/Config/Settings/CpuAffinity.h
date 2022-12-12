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

			virtual std::string getValueStr() const override;

			unsigned get() const { return m_value; }

		private:
			void setValues(const std::vector<std::string>& values) override;

			unsigned m_value;
		};
	}

	extern Settings::CpuAffinity cpuAffinity;
}
