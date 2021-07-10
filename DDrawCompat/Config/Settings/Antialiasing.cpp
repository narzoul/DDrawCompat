#include <d3dtypes.h>
#include <d3dumddi.h>

#include <Config/Settings/Antialiasing.h>

namespace Config
{
	namespace Settings
	{
		Antialiasing::Antialiasing()
			: MappedSetting("Antialiasing", "off", {
				{"off", D3DDDIMULTISAMPLE_NONE},
				{"msaa", D3DDDIMULTISAMPLE_NONMASKABLE},
				{"msaa2x", D3DDDIMULTISAMPLE_2_SAMPLES},
				{"msaa4x", D3DDDIMULTISAMPLE_4_SAMPLES},
				{"msaa8x", D3DDDIMULTISAMPLE_8_SAMPLES}
				})
			, m_param(0)
		{
		}

		std::string Antialiasing::getParamStr() const
		{
			return D3DDDIMULTISAMPLE_NONE != m_value ? std::to_string(m_param) : std::string();
		}

		void Antialiasing::setDefaultParam(const UINT& value)
		{
			m_param = D3DDDIMULTISAMPLE_NONE != value ? 7 : 0;
		}

		void Antialiasing::setValue(const UINT& value, const std::string& param)
		{
			if (D3DDDIMULTISAMPLE_NONE != value)
			{
				const UINT p = Config::Parser::parseUnsigned(param);
				if (p <= 7)
				{
					m_value = value;
					m_param = p;
					return;
				}
			}
			throw ParsingError("invalid parameter: '" + param + "'");
		}
	}
}
