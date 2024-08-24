#include <sstream>

#include <Common/Hook.h>
#include <Common/Log.h>
#include <Common/Path.h>
#include <Config/Settings/WinVersionLie.h>
#include <Dll/Dll.h>

#include <Win32/Version.h>

#pragma warning(disable : 4996)

namespace
{
	DWORD getModuleFileName(HMODULE mod, char* filename, DWORD size)
	{
		return GetModuleFileNameA(mod, filename, size);
	}

	DWORD getModuleFileName(HMODULE mod, wchar_t* filename, DWORD size)
	{
		return GetModuleFileNameW(mod, filename, size);
	}

	HMODULE getModuleHandle(const char* moduleName)
	{
		return GetModuleHandleA(moduleName);
	}

	HMODULE getModuleHandle(const wchar_t* moduleName)
	{
		return GetModuleHandleW(moduleName);
	}

	template <typename Char>
	void fixVersionInfoFileName(const Char*& filename)
	{
		if (getModuleHandle(filename) == Dll::g_currentModule)
		{
			static Char path[MAX_PATH];
			if (0 != getModuleFileName(Dll::g_origDDrawModule, path, MAX_PATH))
			{
				filename = path;
			}
		}
	}

	template <auto func, typename Result, typename Char, typename... Params>
	Result WINAPI getFileVersionInfoFunc(const Char* filename ,Params... params)
	{
		LOG_FUNC(Compat::g_origFuncName<func>.c_str(), filename, params...);
		fixVersionInfoFileName(filename);
		return LOG_RESULT(CALL_ORIG_FUNC(func)(filename, params...));
	}

	template <auto func, typename Result, typename Char, typename... Params>
	Result WINAPI getFileVersionInfoFunc(DWORD flags, const Char* filename, Params... params)
	{
		LOG_FUNC(Compat::g_origFuncName<func>.c_str(), filename, params...);
		fixVersionInfoFileName(filename);
		return LOG_RESULT(CALL_ORIG_FUNC(func)(flags, filename, params...));
	}

	DWORD WINAPI getVersion()
	{
		LOG_FUNC("GetVersion");
		auto vi = Config::winVersionLie.get();
		if (0 != vi.version)
		{
			return LOG_RESULT(vi.version);
		}
		return LOG_RESULT(CALL_ORIG_FUNC(GetVersion)());
	}

	template <typename OsVersionInfoEx, typename OsVersionInfo>
	BOOL getVersionInfo(OsVersionInfo* osVersionInfo, BOOL(WINAPI* origGetVersionInfo)(OsVersionInfo*),
		[[maybe_unused]] const char* funcName)
	{
		LOG_FUNC(funcName, osVersionInfo);
		BOOL result = origGetVersionInfo(osVersionInfo);
		auto vi = Config::winVersionLie.get();
		if (result && 0 != vi.version)
		{
			osVersionInfo->dwMajorVersion = vi.version & 0xFF;
			osVersionInfo->dwMinorVersion = (vi.version & 0xFF00) >> 8;
			osVersionInfo->dwBuildNumber = vi.build;
			osVersionInfo->dwPlatformId = vi.platform;

			auto sp = Config::winVersionLie.getParam();
			if (0 != sp)
			{
				typedef std::remove_reference_t<decltype(osVersionInfo->szCSDVersion[0])> Char;
				std::basic_ostringstream<Char> oss;
				oss << "Service Pack " << sp;
				memset(osVersionInfo->szCSDVersion, 0, sizeof(osVersionInfo->szCSDVersion));
				memcpy(osVersionInfo->szCSDVersion, oss.str().c_str(), oss.str().length() * sizeof(Char));

				if (osVersionInfo->dwOSVersionInfoSize >= sizeof(OsVersionInfoEx))
				{
					auto osVersionInfoEx = reinterpret_cast<OsVersionInfoEx*>(osVersionInfo);
					osVersionInfoEx->wServicePackMajor = static_cast<WORD>(sp);
					osVersionInfoEx->wServicePackMinor = 0;
				}
			}
		}
		return result;
	}

	BOOL WINAPI getVersionExA(LPOSVERSIONINFOA lpVersionInformation)
	{
		return getVersionInfo<OSVERSIONINFOEXA>(lpVersionInformation, CALL_ORIG_FUNC(GetVersionExA), "GetVersionExA");
	}

	BOOL WINAPI getVersionExW(LPOSVERSIONINFOW lpVersionInformation)
	{
		return getVersionInfo<OSVERSIONINFOEXW>(lpVersionInformation, CALL_ORIG_FUNC(GetVersionExW), "GetVersionExW");
	}

	template <auto origFunc>
	bool hookVersionInfoFunc(const char* moduleName, const char* funcName)
	{
		HMODULE mod = GetModuleHandle(moduleName);
		if (mod)
		{
			FARPROC func = Compat::getProcAddress(mod, funcName);
			if (func)
			{
				Compat::hookFunction<origFunc>(moduleName, funcName, getFileVersionInfoFunc<origFunc>);
				return true;
			}
		}
		return false;
	}

	template <auto origFunc>
	void hookVersionInfoFunc(const char* funcName)
	{
		hookVersionInfoFunc<origFunc>("kernelbase", funcName) || hookVersionInfoFunc<origFunc>("version", funcName);
	}
}

namespace Compat
{
	template<> decltype(&GetFileVersionInfoExA) g_origFuncPtr<GetFileVersionInfoExA> = nullptr;
	template<> decltype(&GetFileVersionInfoSizeExA) g_origFuncPtr<GetFileVersionInfoSizeExA> = nullptr;
}

#define HOOK_VERSION_INFO_FUNCTION(func) hookVersionInfoFunc<func>(#func)

namespace Win32
{
	namespace Version
	{
		void installHooks()
		{
			HOOK_VERSION_INFO_FUNCTION(GetFileVersionInfoA);
			HOOK_VERSION_INFO_FUNCTION(GetFileVersionInfoW);
			HOOK_VERSION_INFO_FUNCTION(GetFileVersionInfoExA);
			HOOK_VERSION_INFO_FUNCTION(GetFileVersionInfoExW);
			HOOK_VERSION_INFO_FUNCTION(GetFileVersionInfoSizeA);
			HOOK_VERSION_INFO_FUNCTION(GetFileVersionInfoSizeW);
			HOOK_VERSION_INFO_FUNCTION(GetFileVersionInfoSizeExA);
			HOOK_VERSION_INFO_FUNCTION(GetFileVersionInfoSizeExW);

			HOOK_FUNCTION(kernel32, GetVersion, getVersion);
			HOOK_FUNCTION(kernel32, GetVersionExA, getVersionExA);
			HOOK_FUNCTION(kernel32, GetVersionExW, getVersionExW);
		}
	}
}
