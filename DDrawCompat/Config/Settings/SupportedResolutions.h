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

			virtual std::string getValueStr() const override;

			std::set<SIZE> get() const { return m_resolutions; }

		private:
			void setValues(const std::vector<std::string>& values) override;

			std::set<SIZE> m_resolutions;
		};
	}

	extern Settings::SupportedResolutions supportedResolutions;
}
