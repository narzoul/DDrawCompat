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
			virtual std::string addValue(const std::string& value) override;
			virtual void clear() override;

			unsigned m_value;
		};
	}

	extern Settings::CpuAffinity cpuAffinity;
}
