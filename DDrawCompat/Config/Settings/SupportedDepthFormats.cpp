#include <Config/Settings/SupportedDepthFormats.h>

namespace Config
{
	namespace Settings
	{
		SupportedDepthFormats::SupportedDepthFormats()
			: FormatListSetting("SupportedDepthFormats", "all",
				{
					D3DDDIFMT_D32,
					D3DDDIFMT_D24S8,
					D3DDDIFMT_D24X8,
					D3DDDIFMT_D16,
					D3DDDIFMT_S8D24,
					D3DDDIFMT_X8D24
				},
				{
					{ "16", { D3DDDIFMT_D16 } },
					{ "24", { D3DDDIFMT_D24S8, D3DDDIFMT_D24X8, D3DDDIFMT_S8D24, D3DDDIFMT_X8D24 } },
					{ "32", { D3DDDIFMT_D32 } }
				},
				false)
		{
		}
	}
}
