#include <Config/Settings/DisplayFilter.h>

namespace Config
{
    namespace Settings
    {
        DisplayFilter::DisplayFilter()
            : MappedSetting("DisplayFilter", "bilinear(0)", { {"point", POINT}, {"bilinear", BILINEAR} })
            , m_param(0)
        {
        }

        std::string DisplayFilter::getParamStr() const
        {
            return BILINEAR == m_value ? std::to_string(m_param) : std::string();
        }

        void DisplayFilter::setDefaultParam(const UINT& value)
        {
            m_param = BILINEAR == value ? 100 : 0;
        }

        void DisplayFilter::setValue(const UINT& value, const std::string& param)
        {
            if (BILINEAR == value)
            {
                const UINT p = Config::Parser::parseUnsigned(param);
                if (p <= 100)
                {
                    m_value = BILINEAR;
                    m_param = p;
                    return;
                }
            }
            throw ParsingError("invalid parameter: '" + param + "'");
        }
    }
}
