#pragma once

#include <filesystem>
#include <stdexcept>

namespace Config
{
	class ParsingError : public std::runtime_error
	{
	public:
		ParsingError(const std::string& error) : runtime_error(error) {}
	};

	namespace Parser
	{
		void loadAllConfigFiles(const std::filesystem::path& processPath);
	}
}
