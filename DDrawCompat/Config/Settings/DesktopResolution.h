#pragma once

#include <Windows.h>

#include <Common/Comparison.h>
#include <Config/MappedSetting.h>

namespace Config
{
	namespace Settings
	{
		class DesktopResolution : public MappedSetting<SIZE>
		{
		public:
			static const SIZE DESKTOP;

			DesktopResolution();

		protected:
			std::string getValueStr() const override;
			void setValue(const std::string& value) override;
		};
	}

	extern Settings::DesktopResolution desktopResolution;
}
