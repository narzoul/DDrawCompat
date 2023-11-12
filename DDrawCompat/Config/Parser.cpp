#include <fstream>
#include <locale>

#include <Common/Log.h>
#include <Common/Path.h>
#include <Config/Parser.h>
#include <Config/Setting.h>

namespace
{
	std::filesystem::path g_overlayConfigPath;

	void setConfig(const std::string& name, const std::string& value, const std::string& source);

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

		auto setting = Config::getSetting(name);
		if (!setting)
		{
			throw Config::ParsingError("unknown setting: '" + name + "'");
		}

		try
		{
			setting->set(Config::Parser::tolower(value), source);
		}
		catch (const Config::ParsingError& error)
		{
			throw Config::ParsingError(setting->getName() + ": " + error.what());
		}
	}
}

namespace Config
{
	namespace Parser
	{
		std::filesystem::path getOverlayConfigPath()
		{
			return g_overlayConfigPath;
		}

		void loadAllConfigFiles(const std::filesystem::path& processPath)
		{
			for (auto& setting : Config::getAllSettings()) {
				setting.second.reset();
			}

			std::size_t maxNameLength = 0;
			for (auto& setting : Config::getAllSettings())
			{
				if (setting.second.getName().length() > maxNameLength)
				{
					maxNameLength = setting.second.getName().length();
				}
			}

#if 0
			std::ofstream of("DDrawCompatDefaults.ini", std::ios::out | std::ios::trunc);
			for (auto& setting : Config::getAllSettings())
			{
				const auto& name = setting.second.getName();
				of << "# " << name << std::string(maxNameLength - name.length(), ' ')
					<< " = " << setting.second.getValueStr() << std::endl;
			}
			of.close();
#endif

			loadConfigFile("global", Compat::getEnvPath("PROGRAMDATA") / "DDrawCompat" / "DDrawCompat.ini");
			loadConfigFile("user", Compat::getEnvPath("LOCALAPPDATA") / "DDrawCompat" / "DDrawCompat.ini");
			loadConfigFile("directory", Compat::replaceFilename(processPath, "DDrawCompat.ini"));

			auto processConfigPath(processPath);
			if (Compat::isEqual(processConfigPath.extension(), ".exe"))
			{
				processConfigPath.replace_extension();
			}
			g_overlayConfigPath = processConfigPath;
			processConfigPath.replace_filename(L"DDrawCompat-" + processConfigPath.filename().native() + L".ini");
			loadConfigFile("process", processConfigPath);

			for (auto& setting : Config::getAllSettings()) {
				setting.second.setBaseValue();
			}

			g_overlayConfigPath.replace_filename(L"DDrawCompatOverlay-" + g_overlayConfigPath.filename().native() + L".ini");
			loadConfigFile("overlay", g_overlayConfigPath);

			for (auto& setting : Config::getAllSettings()) {
				setting.second.setExportedValue();
			}

			std::size_t maxSourceLength = 0;
			for (auto& setting : Config::getAllSettings())
			{
				if (setting.second.getSource().length() > maxSourceLength)
				{
					maxSourceLength = setting.second.getSource().length();
				}
			}

			LOG_INFO << "Final configuration:";
			for (auto& setting : Config::getAllSettings())
			{
				std::string name(setting.second.getName());
				name.insert(name.end(), maxNameLength - name.length(), ' ');
				std::string source(setting.second.getSource());
				source.insert(source.end(), maxSourceLength - source.length(), ' ');
				LOG_INFO << "  [" << source << "] " << name << " = " << setting.second.getValueStr();
			}
		}

		SIZE parseAspectRatio(const std::string& value)
		{
			try
			{
				auto pos = value.find(':');
				if (pos != std::string::npos)
				{
					return { parseInt(value.substr(0, pos), 1, MAXUINT16), parseInt(value.substr(pos + 1), 1, MAXUINT16) };
				}
			}
			catch (ParsingError&)
			{
			}

			throw ParsingError("invalid aspect ratio: '" + value + "'");
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

		std::string toupper(const std::string& str)
		{
			std::string result(str);
			for (auto& c : result)
			{
				c = std::toupper(c, std::locale());
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
