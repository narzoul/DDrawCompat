#include <fstream>

#include <Common/Log.h>
#include <Common/Path.h>
#include <Config/Parser.h>

namespace
{
	void setConfig(const std::string& name, const std::string& value);
	std::string trim(const std::string& str);

	void loadConfigFile(const std::string& type, const std::filesystem::path& path)
	{
		Compat::Log() << "Loading " << type << " config file: " << path.u8string();
		std::ifstream f(path);
		if (!f.is_open())
		{
			Compat::Log() << "  File not found, skipping";
			return;
		}

		unsigned lineNumber = 0;
		std::string line;
		while (std::getline(f, line))
		{
			try {
				++lineNumber;
				auto pos = line.find_first_of(";#");
				if (pos != std::string::npos)
				{
					line.resize(pos);
				}

				if (line.find_first_not_of(" \t") == std::string::npos)
				{
					continue;
				}

				pos = line.find('=');
				if (pos == std::string::npos)
				{
					throw Config::ParsingError("missing '=' separator");
				}

				setConfig(trim(line.substr(0, pos)), trim(line.substr(pos + 1)));
			}
			catch (const Config::ParsingError& error)
			{
				Compat::Log() << "  Line #" << lineNumber << ": " << error.what();
			}
		}
	}

	void setConfig(const std::string& name, const std::string& /*value*/)
	{
		if (name.empty())
		{
			throw Config::ParsingError("missing setting name before '='");
		}

		throw Config::ParsingError("unknown setting: '" + name + "'");
	}

	std::string trim(const std::string& str)
	{
		auto result(str);
		auto pos = str.find_last_not_of(" \t");
		if (pos != std::string::npos)
		{
			result.resize(pos + 1);
		}

		pos = result.find_first_not_of(" \t");
		if (pos != std::string::npos)
		{
			result = result.substr(pos);
		}
		return result;
	}
}

namespace Config
{
	namespace Parser
	{
		void loadAllConfigFiles(const std::filesystem::path& processPath)
		{
			loadConfigFile("global", Compat::getEnvPath("PROGRAMDATA") / "DDrawCompat" / "DDrawCompat.ini");
			loadConfigFile("user", Compat::getEnvPath("LOCALAPPDATA") / "DDrawCompat" / "DDrawCompat.ini");
			loadConfigFile("directory", Compat::replaceFilename(processPath, "DDrawCompat.ini"));

			auto processConfigPath(processPath);
			if (Compat::isEqual(processConfigPath.extension(), ".exe"))
			{
				processConfigPath.replace_extension();
			}
			processConfigPath.replace_filename(L"DDrawCompat-" + processConfigPath.filename().native() + L".ini");
			loadConfigFile("process", processConfigPath);
		}
	}
}
