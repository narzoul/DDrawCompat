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
			virtual std::string addValue(const std::string& value) override;
			virtual void clear() override;

			std::set<SIZE> m_resolutions;
		};
	}

	extern Settings::SupportedResolutions supportedResolutions;
}
