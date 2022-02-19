#pragma once

#include <filesystem>
#include <stdexcept>
#include <string>

#include <Windows.h>

namespace Config
{
	class ParsingError : public std::runtime_error
	{
	public:
		ParsingError(const std::string& error) : runtime_error(error) {}
	};

	class Setting;

	namespace Parser
	{
		void loadAllConfigFiles(const std::filesystem::path& processPath);
		SIZE parseResolution(const std::string& value);
		int parseInt(const std::string& value, int min, int max);
		void registerSetting(Setting& setting);
		std::string removeParam(const std::string& value);
		std::string tolower(const std::string& str);
		std::string trim(const std::string& str);
	}
}
