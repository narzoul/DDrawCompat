#pragma once

#include <filesystem>
#include <functional>
#include <string>

#include <Windows.h>

namespace Compat
{
	void forEachFile(const std::wstring& path, std::function<void(const WIN32_FIND_DATAW&)> callback);
	std::filesystem::path getEnvPath(const char* envVar);
	std::filesystem::path getModulePath(HMODULE module);
	std::filesystem::path getSystemPath();
	std::filesystem::path getWindowsPath();
	bool isEqual(const std::filesystem::path& p1, const std::filesystem::path& p2);
	bool isPrefix(const std::filesystem::path& p1, const std::filesystem::path& p2);
	std::filesystem::path replaceFilename(const std::filesystem::path& path, const std::filesystem::path& filename);
}
