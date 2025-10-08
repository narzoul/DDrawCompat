#pragma once

#include <Config/EnumSetting.h>
#include <D3dDdi/ShaderCompiler.h>

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
			const std::map<std::string, float>& getCgpParameters() const { return m_cgpParameters; }
			void setCgpParameters(const std::vector<D3dDdi::ShaderCompiler::Parameter>& parameters);

		protected:
			std::string getValueStr() const override;
			void setValue(const std::string& value) override;

		private:
			std::filesystem::path m_cgpPath;
			std::map<std::string, float> m_cgpParameters;
		};
	}

	extern Settings::DisplayFilter displayFilter;
}
