#include <Config/Settings/DisplayFilter.h>
#include <D3dDdi/MetaShader.h>

namespace Config
{
	namespace Settings
	{
		DisplayFilter::DisplayFilter()
			: EnumSetting("DisplayFilter", "bilinear", { "point", "integer", "bilinear", "bicubic", "lanczos", "spline" })
		{
		}

		std::vector<std::string> DisplayFilter::getDefaultValueStrings()
		{
			if (m_defaultValueStrings.empty())
			{
				EnumSetting::getDefaultValueStrings();
				std::set<std::string> paths;
				const auto& baseDirs = D3dDdi::MetaShader::getBaseDirs();
				for (const auto& baseDir : baseDirs)
				{
					std::error_code ec;
					auto iter = std::filesystem::recursive_directory_iterator(baseDir, ec);
					if (ec)
					{
						continue;
					}

					for (auto p = std::filesystem::begin(iter); p != std::filesystem::end(iter); p.increment(ec))
					{
						if (!p->is_directory(ec) && p->path().extension() == ".cgp")
						{
							paths.insert(p->path().lexically_relative(baseDir).u8string());
						}
					}
				}
				m_defaultValueStrings.insert(m_defaultValueStrings.end(), paths.begin(), paths.end());
			}
			return m_defaultValueStrings;
		}

		Setting::ParamInfo DisplayFilter::getParamInfo() const
		{
			switch (m_value)
			{
			case BILINEAR:
			case BICUBIC:
				return { "Blur", 0, 100, 0 };
			case LANCZOS:
			case SPLINE:
				return { "Lobes", 2, 4, 2 };
			case CGP:
				return { "cgp" };
			default:
				return {};
			}
		}

		std::string DisplayFilter::getValueStr() const
		{
			if (CGP == m_value)
			{
				return m_cgpPath.u8string();
			}
			return MappedSetting::getValueStr();
		}

		void DisplayFilter::setValue(const std::string& value)
		{
			if (value.length() > 4 && 0 == strcmp(value.c_str() + value.length() - 4, ".cgp"))
			{
				for (const auto& v : getDefaultValueStrings())
				{
					if (0 == _stricmp(v.c_str(), value.c_str()))
					{
						m_value = CGP;
						m_cgpPath = v;
						return;
					}
				}
				throw Config::ParsingError("file not found: '" + value + "'");
			}
			else
			{
				MappedSetting::setValue(value);
				m_cgpPath.clear();
			}
		}
	}
}
