#pragma once

#include <Windows.h>

#include <Common/Comparison.h>
#include <Config/MappedSetting.h>

namespace Config
{
	namespace Settings
	{
		class ResolutionScale : public MappedSetting<SIZE>
		{
		public:
			static const SIZE APP;
			static const SIZE DISPLAY;

			ResolutionScale();

			virtual ParamInfo getParamInfo() const override;

		protected:
			std::string getValueStr() const override;
			void setValue(const std::string& value) override;
		};
	}
}
