#include <Common/Path.h>
#include <Dll/Dll.h>

namespace Compat
{
	void forEachFile(const std::wstring& path, std::function<void(const WIN32_FIND_DATAW&)> callback)
	{
		WIN32_FIND_DATAW fd = {};
		HANDLE find = FindFirstFileW((path).c_str(), &fd);
		if (INVALID_HANDLE_VALUE == find)
		{
			return;
		}

		do
		{
			if (0 != wcscmp(fd.cFileName, L".") &&
				0 != wcscmp(fd.cFileName, L".."))
			{
				callback(fd);
			}
		} while (FindNextFileW(find, &fd));

		FindClose(find);
	}

	std::filesystem::path getEnvPath(const char* envVar)
	{
		return Dll::getEnvVar(envVar);
	}

	std::filesystem::path getModulePath(HMODULE module)
	{
		wchar_t path[MAX_PATH] = {};
		GetModuleFileNameW(module, path, MAX_PATH);
		return path;
	}

	std::filesystem::path getSystemPath()
	{
		wchar_t path[MAX_PATH] = {};
		GetSystemDirectoryW(path, MAX_PATH);
		return path;
	}

	std::filesystem::path getWindowsPath()
	{
		wchar_t path[MAX_PATH] = {};
		GetWindowsDirectoryW(path, MAX_PATH);
		return path;
	}

	bool isEqual(const std::filesystem::path& p1, const std::filesystem::path& p2)
	{
		return 0 == _wcsicmp(p1.c_str(), p2.c_str());
	}

	bool isPrefix(const std::filesystem::path& p1, const std::filesystem::path& p2)
	{
		const auto& n1 = p1.native();
		const auto& n2 = p2.native();
		if (n1.length() > n2.length())
		{
			return false;
		}
		return 0 == _wcsicmp(n1.c_str(), n2.substr(0, n1.length()).c_str());
	}

	std::filesystem::path replaceFilename(const std::filesystem::path& path, const std::filesystem::path& filename)
	{
		std::filesystem::path result(path);
		result.replace_filename(filename);
		return result;
	}
}
