#pragma once

#include <set>

#include <Windows.h>

#include <Config/ListSetting.h>

namespace Config
{
	namespace Settings
	{
		class SupportedDevices : public ListSetting
		{
		public:
			SupportedDevices();

			bool isSupported(const IID& deviceType) const;

		private:
			virtual std::string addValue(const std::string& value) override;
			virtual void clear() override;

			std::set<IID> m_devices;
			bool m_allowAll;
		};
	}

	extern Settings::SupportedDevices supportedDevices;
}
