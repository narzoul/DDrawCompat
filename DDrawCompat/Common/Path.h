#pragma once

#include <filesystem>

#include <Windows.h>

namespace Compat
{
	std::filesystem::path getEnvPath(const char* envVar);
	std::filesystem::path getModulePath(HMODULE module);
	std::filesystem::path getSystemPath();
	bool isEqual(const std::filesystem::path& p1, const std::filesystem::path& p2);
	bool isPrefix(const std::filesystem::path& p1, const std::filesystem::path& p2);
	std::filesystem::path replaceFilename(const std::filesystem::path& path, const std::filesystem::path& filename);
}
