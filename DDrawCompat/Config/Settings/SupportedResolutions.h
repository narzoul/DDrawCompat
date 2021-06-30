#pragma once

#include <set>

#include <Windows.h>

#include <Config/ListSetting.h>

namespace Config
{
	namespace Settings
	{
		class SupportedResolutions : public ListSetting
		{
		public:
			static const SIZE NATIVE;

			SupportedResolutions();

			std::set<SIZE> get() const { return m_resolutions; }

		private:
			std::string getValueStr() const override;
			void setValues(const std::vector<std::string>& values) override;

			std::set<SIZE> m_resolutions;
		};
	}
}
