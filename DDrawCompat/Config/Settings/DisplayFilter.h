#pragma once

#include <Config/MappedSetting.h>

namespace Config
{
    namespace Settings
    {
        class DisplayFilter : public MappedSetting<UINT>
        {
        public:
            static const UINT POINT = 0;
            static const UINT BILINEAR = 1;

            DisplayFilter();

            UINT getParam() const { return m_param; }

        protected:
            virtual std::string getParamStr() const override;
            virtual void setDefaultParam(const UINT& value) override;
            virtual void setValue(const UINT& value, const std::string& param) override;

            UINT m_param;
        };
    }
}
