#include <Config/Settings/SupportedTextureFormats.h>

#define FOURCC(cc) *reinterpret_cast<D3DDDIFORMAT*>(const_cast<char*>(#cc))

namespace Config
{
	namespace Settings
	{
		SupportedTextureFormats::SupportedTextureFormats()
			: FormatListSetting("SupportedTextureFormats", "all",
				{
					D3DDDIFMT_R8G8B8,
					D3DDDIFMT_A8R8G8B8,
					D3DDDIFMT_X8R8G8B8,
					D3DDDIFMT_R5G6B5,
					D3DDDIFMT_X1R5G5B5,
					D3DDDIFMT_A1R5G5B5,
					D3DDDIFMT_A4R4G4B4,
					D3DDDIFMT_R3G3B2,
					D3DDDIFMT_A8,
					D3DDDIFMT_A8R3G3B2,
					D3DDDIFMT_X4R4G4B4,
					D3DDDIFMT_A8P8,
					D3DDDIFMT_P8,
					D3DDDIFMT_L8,
					D3DDDIFMT_A8L8,
					D3DDDIFMT_A4L4,
					D3DDDIFMT_V8U8,
					D3DDDIFMT_L6V5U5,
					D3DDDIFMT_X8L8V8U8
				},
				{
					{ "argb", { D3DDDIFMT_A8R8G8B8, D3DDDIFMT_A1R5G5B5, D3DDDIFMT_A4R4G4B4 } },
					{ "bump", { D3DDDIFMT_V8U8, D3DDDIFMT_L6V5U5, D3DDDIFMT_X8L8V8U8 } },
					{ "dxt", { FOURCC(DXT1), FOURCC(DXT2), FOURCC(DXT3), FOURCC(DXT4),FOURCC(DXT5) } },
					{ "lum", { D3DDDIFMT_L8, D3DDDIFMT_A8L8, D3DDDIFMT_A4L4 } },
					{ "rgb", { D3DDDIFMT_X8R8G8B8, D3DDDIFMT_R5G6B5, D3DDDIFMT_X1R5G5B5, D3DDDIFMT_X4R4G4B4 } }
				},
				true)
		{
		}
	}
}
