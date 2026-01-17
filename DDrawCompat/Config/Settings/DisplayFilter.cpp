#include <regex>
#include <set>
#include <sstream>

#include <Windows.h>

#include <Common/Path.h>
#include <Config/Settings/DisplayFilter.h>
#include <D3dDdi/MetaShader.h>

namespace
{
	void enumPath(std::set<std::string>& paths, std::vector<std::wstring>& tmpFiles,
		const std::filesystem::path& baseDir, const std::filesystem::path& dir)
	{
		Compat::forEachFile((dir / "*").native(), [&](const WIN32_FIND_DATAW& fd)
			{
				const std::filesystem::path p(dir / fd.cFileName);
				if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
				{
					enumPath(paths, tmpFiles, baseDir, p);
					return;
				}

				if (p.extension() == ".cgp")
				{
					paths.insert(p.lexically_relative(baseDir).string());
				}
				else if (p.extension() == ".tmp")
				{
					auto fn(p.filename());
					fn.replace_extension();
					if (fn.extension() == ".dcc")
					{
						tmpFiles.push_back(p);
					}
				}
			});
	}
}

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
				std::vector<std::wstring> tmpFiles;
				const auto& baseDirs = D3dDdi::MetaShader::getBaseDirs();
				for (const auto& baseDir : baseDirs)
				{
					enumPath(paths, tmpFiles, baseDir, baseDir);
				}

				for (const auto& tmpFile : tmpFiles)
				{
					DeleteFileW(tmpFile.c_str());
				}

				m_defaultValueStrings.insert(m_defaultValueStrings.end(), paths.begin(), paths.end());
			}

			if (!m_cgpParameters.empty())
			{
				auto it = std::find_if(m_defaultValueStrings.begin(), m_defaultValueStrings.end(),
					[&](const auto& v) { return v == m_cgpPath; });
				if (it != m_defaultValueStrings.end())
				{
					++it;
					if (it != m_defaultValueStrings.end() && it->substr(0, it->find(':')) == m_cgpPath)
					{
						it = m_defaultValueStrings.erase(it);
					}
					m_defaultValueStrings.insert(it, getValueStr());
				}
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
				std::ostringstream oss;
				oss << m_cgpPath.string();
				for (const auto& param : m_cgpParameters)
				{
					oss << ':' << param.first << '=' << std::fixed << std::setprecision(3) << param.second;
				}
				return oss.str();
			}
			return MappedSetting::getValueStr();
		}

		void DisplayFilter::setCgpParameters(const std::vector<D3dDdi::ShaderCompiler::Parameter>& parameters)
		{
			m_cgpParameters.clear();
			for (const auto& param : parameters)
			{
				if (param.currentValue != param.defaultValue)
				{
					m_cgpParameters[Parser::tolower(param.name)] = param.currentValue;
				}
			}
		}

		void DisplayFilter::setValue(const std::string& value)
		{
			auto paramSeparatorPos = std::min(value.find(':'), value.length());
			const std::string_view path(value.data(), paramSeparatorPos);
			if (path.length() > 4 && ".cgp" == path.substr(path.length() - 4))
			{
				std::map<std::string, float> cgpParameters;
				std::string_view params(value.data() + paramSeparatorPos, value.length() - paramSeparatorPos);
				const std::regex paramSpecRegex(R"(([a-z_][0-9a-z_]*)=(-?[0-9]+(\.[0-9]+)?))");
				while (!params.empty())
				{
					paramSeparatorPos = std::min(params.find(':', 1), params.length());
					const std::string_view param(params.data() + 1, paramSeparatorPos - 1);
					std::match_results<std::string_view::const_iterator> match;
					if (!std::regex_match(param.begin(), param.end(), match, paramSpecRegex))
					{
						throw ParsingError("invalid parameter specification: '" + std::string(param) + "'");
					}

					std::istringstream iss(match[2]);
					float paramValue = 0;
					iss >> paramValue;
					cgpParameters[match[1]] = paramValue;

					params = params.substr(paramSeparatorPos);
				}

				for (const auto& v : getDefaultValueStrings())
				{
					std::string p(path);
					if (0 == _stricmp(v.c_str(), p.c_str()))
					{
						m_value = CGP;
						m_cgpPath = v;
						m_cgpParameters = cgpParameters;
						return;
					}
				}
				throw ParsingError("file not found: '" + std::string(path) + "'");
			}
			else
			{
				MappedSetting::setValue(value);
				m_cgpPath.clear();
				m_cgpParameters.clear();
			}
		}
	}
}
