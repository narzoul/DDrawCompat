#pragma once

#include <filesystem>
#include <stdexcept>
#include <string>

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
		unsigned parseUnsigned(const std::string& value);
		void registerSetting(Setting& setting);
		std::string trim(const std::string& str);
	}
}
