#pragma once

#include <Config/MappedSetting.h>

namespace Config
{
	namespace Settings
	{
		class DisplayRefreshRate : public MappedSetting<int>
		{
		public:
			static const int APP = 0;
			static const int DESKTOP = -1;

			DisplayRefreshRate();

		protected:
			std::string getValueStr() const override;
			void setValue(const std::string& value) override;
		};
	}
}
