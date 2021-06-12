#pragma once

#include <Windows.h>

#include <Common/Comparison.h>
#include <Config/MappedSetting.h>

namespace Config
{
	namespace Settings
	{
		class DisplayResolution : public MappedSetting<SIZE>
		{
		public:
			static const SIZE APP;
			static const SIZE DESKTOP;

			DisplayResolution();

		protected:
			std::string getValueStr() const override;
			void setValue(const std::string& value) override;
		};
	}
}
