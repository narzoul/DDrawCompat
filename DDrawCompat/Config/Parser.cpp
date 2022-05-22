#include <fstream>
#include <locale>
#include <map>

#include <Common/Log.h>
#include <Common/Path.h>
#include <Config/Parser.h>
#include <Config/Setting.h>

namespace
{
	void setConfig(const std::string& name, const std::string& value, const std::string& source);

	auto& getSettings()
	{
		static std::map<std::string, Config::Setting&> settings;
		return settings;
	}

	void loadConfigFile(const std::string& source, const std::filesystem::path& path)
	{
		LOG_INFO << "Loading " << source << " config file: " << path.u8string();
		std::ifstream f(path);
		if (!f.is_open())
		{
			LOG_INFO << "  File not found, skipping";
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

				setConfig(Config::Parser::trim(line.substr(0, pos)), Config::Parser::trim(line.substr(pos + 1)), source);
			}
			catch (const Config::ParsingError& error)
			{
				LOG_INFO << "  Line #" << lineNumber << ": " << error.what();
			}
		}
	}

	void setConfig(const std::string& name, const std::string& value, const std::string& source)
	{
		if (name.empty())
		{
			throw Config::ParsingError("missing setting name before '='");
		}

		auto it = std::find_if(getSettings().cbegin(), getSettings().cend(), [&](const auto& v) {
			return 0 == _stricmp(v.first.c_str(), name.c_str()); });
		if (it == getSettings().end())
		{
			throw Config::ParsingError("unknown setting: '" + name + "'");
		}

		try
		{
			it->second.set(Config::Parser::tolower(value), source);
		}
		catch (const Config::ParsingError& error)
		{
			throw Config::ParsingError(it->second.getName() + ": " + error.what());
		}
	}
}

namespace Config
{
	namespace Parser
	{
		void loadAllConfigFiles(const std::filesystem::path& processPath)
		{
			for (auto& setting : getSettings()) {
				setting.second.reset();
			}

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

			std::size_t maxNameLength = 0;
			std::size_t maxSourceLength = 0;
			for (const auto& setting : getSettings())
			{
				if (setting.second.getName().length() > maxNameLength)
				{
					maxNameLength = setting.second.getName().length();
				}
				if (setting.second.getSource().length() > maxSourceLength)
				{
					maxSourceLength = setting.second.getSource().length();
				}
			}

			LOG_INFO << "Final configuration:";
			for (const auto& setting : getSettings())
			{
				std::string name(setting.second.getName());
				name.insert(name.end(), maxNameLength - name.length(), ' ');
				std::string source(setting.second.getSource());
				source.insert(source.end(), maxSourceLength - source.length(), ' ');
				LOG_INFO << "  [" << source << "] " << name << " = " << setting.second.getValueStr();
			}
		}

		int parseInt(const std::string& value, int min, int max)
		{
			if (value.empty() || std::string::npos != value.find_first_not_of("+-0123456789") ||
				std::string::npos != value.substr(1).find_first_of("+-"))
			{
				throw ParsingError("not a valid integer: '" + value + "'");
			}

			int result = std::strtol(value.c_str(), nullptr, 10);
			if (result < min || result > max)
			{
				throw ParsingError("integer out of range: '" + value + "'");
			}
			return result;
		}

		SIZE parseResolution(const std::string& value)
		{
			try
			{
				auto pos = value.find('x');
				if (pos != std::string::npos)
				{
					return { parseInt(value.substr(0, pos), 1, MAXUINT16), parseInt(value.substr(pos + 1), 1, MAXUINT16) };
				}
			}
			catch (ParsingError&)
			{
			}

			throw ParsingError("invalid resolution: '" + value + "'");
		}

		void registerSetting(Setting& setting)
		{
			const auto& name = setting.getName();
			getSettings().emplace(name, setting);
		}

		std::string removeParam(const std::string& value)
		{
			auto paramPos = value.find('(');
			if (paramPos != std::string::npos)
			{
				return value.substr(0, paramPos);
			}
			return value;
		}

		std::string tolower(const std::string& str)
		{
			std::string result(str);
			for (auto& c : result)
			{
				c = std::tolower(c, std::locale());
			}
			return result;
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
}
