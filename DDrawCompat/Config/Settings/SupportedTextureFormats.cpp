#include <map>
#include <vector>

#include <d3d.h>
#include <d3dumddi.h>

#include <Config/Parser.h>
#include <Config/Settings/SupportedTextureFormats.h>

namespace
{
	std::map<D3DDDIFORMAT, std::string> g_formatNames = []()
	{
		std::map<D3DDDIFORMAT, std::string> names;

#define ADD_FORMAT(format) names[format] = #format
		ADD_FORMAT(D3DDDIFMT_R8G8B8);
		ADD_FORMAT(D3DDDIFMT_A8R8G8B8);
		ADD_FORMAT(D3DDDIFMT_X8R8G8B8);
		ADD_FORMAT(D3DDDIFMT_R5G6B5);
		ADD_FORMAT(D3DDDIFMT_X1R5G5B5);
		ADD_FORMAT(D3DDDIFMT_A1R5G5B5);
		ADD_FORMAT(D3DDDIFMT_A4R4G4B4);
		ADD_FORMAT(D3DDDIFMT_R3G3B2);
		ADD_FORMAT(D3DDDIFMT_A8);
		ADD_FORMAT(D3DDDIFMT_A8R3G3B2);
		ADD_FORMAT(D3DDDIFMT_X4R4G4B4);
		ADD_FORMAT(D3DDDIFMT_A8P8);
		ADD_FORMAT(D3DDDIFMT_P8);
		ADD_FORMAT(D3DDDIFMT_L8);
		ADD_FORMAT(D3DDDIFMT_A8L8);
		ADD_FORMAT(D3DDDIFMT_A4L4);
		ADD_FORMAT(D3DDDIFMT_V8U8);
		ADD_FORMAT(D3DDDIFMT_L6V5U5);
		ADD_FORMAT(D3DDDIFMT_X8L8V8U8);
#undef  ADD_FORMAT

		for (auto& pair : names)
		{
			pair.second = Config::Parser::tolower(pair.second.substr(10));
		}
		return names;
	}();

#define FOURCC(cc) *reinterpret_cast<D3DDDIFORMAT*>(#cc)
	std::map<std::string, std::vector<D3DDDIFORMAT>> g_formatGroups = {
		{ "argb", { D3DDDIFMT_A8R8G8B8, D3DDDIFMT_A1R5G5B5, D3DDDIFMT_A4R4G4B4 } },
		{ "bump", { D3DDDIFMT_V8U8, D3DDDIFMT_L6V5U5, D3DDDIFMT_X8L8V8U8 } },
		{ "dxt", { FOURCC(DXT1), FOURCC(DXT2), FOURCC(DXT3), FOURCC(DXT4),FOURCC(DXT5) } },
		{ "lum", { D3DDDIFMT_L8, D3DDDIFMT_A8L8, D3DDDIFMT_A4L4 } },
		{ "rgb", { D3DDDIFMT_X8R8G8B8, D3DDDIFMT_R5G6B5, D3DDDIFMT_X1R5G5B5, D3DDDIFMT_X4R4G4B4 } }
	};
#undef FOURCC
}

namespace Config
{
	namespace Settings
	{
		SupportedTextureFormats::SupportedTextureFormats()
			: ListSetting("SupportedTextureFormats", "all")
		{
		}

		std::string SupportedTextureFormats::getValueStr() const
		{
			if (m_formats.empty())
			{
				return "all";
			}

			std::string result;
			for (const auto& format : m_formats)
			{
				result += ", ";
				auto it = g_formatNames.find(static_cast<D3DDDIFORMAT>(format));
				if (it != g_formatNames.end())
				{
					result += Config::Parser::toupper(it->second);
				}
				else
				{
					auto p = reinterpret_cast<const char*>(&format);
					result += std::string(p, p + 4);
				}
			}
			return result.substr(2);
		}

		bool SupportedTextureFormats::isSupported(UINT format) const
		{
			if (m_formats.empty())
			{
				return true;
			}
			return m_formats.find(format) != m_formats.end();
		}

		void SupportedTextureFormats::setValues(const std::vector<std::string>& values)
		{
			if (values.empty())
			{
				throw ParsingError("empty list is not allowed");
			}

			if (1 == values.size() && "all" == values[0])
			{
				m_formats.clear();
				return;
			}

			std::set<UINT> formats;
			for (const auto& fmt : values)
			{
				if ("all" == fmt)
				{
					throw ParsingError("'all' cannot be combined with other values");
				}

				auto group = g_formatGroups.find(fmt);
				if (group != g_formatGroups.end())
				{
					formats.insert(group->second.begin(), group->second.end());
					continue;
				}
				
				auto name = std::find_if(g_formatNames.begin(), g_formatNames.end(),
					[&](const auto& pair) { return pair.second == fmt; });
				if (name != g_formatNames.end())
				{
					formats.insert(name->first);
					continue;
				}

				if (4 == fmt.length() &&
					fmt.end() == std::find_if(fmt.begin(), fmt.end(), [](const char c) { return !std::isalnum(c); }))
				{
					formats.insert(*reinterpret_cast<const UINT*>(Config::Parser::toupper(fmt).c_str()));
					continue;
				}

				throw ParsingError("invalid format name: '" + fmt + "'");
			}

			m_formats = formats;
		}
	}
}
