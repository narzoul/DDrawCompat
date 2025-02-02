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
		std::filesystem::path getOverlayConfigPath();
		void loadAllConfigFiles(const std::filesystem::path& processPath);
		void logError(const std::string& error);
		SIZE parseAspectRatio(const std::string& value);
		int parseInt(const std::string& value, int min, int max);
		SIZE parseResolution(const std::string& value);
		std::string removeParam(const std::string& value);
		std::string tolower(const std::string& str);
		std::string toupper(const std::string& str);
		std::string trim(const std::string& str);
	}
}
