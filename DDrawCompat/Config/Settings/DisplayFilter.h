#pragma once

#include <Config/EnumSetting.h>

namespace Config
{
	namespace Settings
	{
		class DisplayFilter : public EnumSetting
		{
		public:
			enum Values { POINT, INTEGER, BILINEAR, BICUBIC, LANCZOS, SPLINE, CGP };

			DisplayFilter();

			virtual std::vector<std::string> getDefaultValueStrings() override;
			virtual ParamInfo getParamInfo() const override;
			const std::filesystem::path& getCgpPath() { return m_cgpPath; }

		protected:
			std::string getValueStr() const override;
			void setValue(const std::string& value) override;

		private:
			std::filesystem::path m_cgpPath;
		};
	}

	extern Settings::DisplayFilter displayFilter;
}
