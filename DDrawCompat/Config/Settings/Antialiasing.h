#pragma once

#include <Config/MappedSetting.h>

namespace Config
{
	namespace Settings
	{
		class Antialiasing : public MappedSetting<UINT>
		{
		public:
			Antialiasing();

			UINT getParam() const { return m_param; }

		protected:
			virtual std::string getParamStr() const override;
			virtual void setDefaultParam(const UINT& value) override;
			virtual void setValue(const UINT& value, const std::string& param) override;

			UINT m_param;
		};
	}
}
