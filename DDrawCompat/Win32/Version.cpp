#include <sstream>

#include <Common/Hook.h>
#include <Common/Log.h>
#include <Config/Settings/WinVersionLie.h>

#include <Win32/Version.h>

#pragma warning(disable : 4996)

namespace
{
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
}

namespace Win32
{
	namespace Version
	{
		void installHooks()
		{
			HOOK_FUNCTION(kernel32, GetVersion, getVersion);
			HOOK_FUNCTION(kernel32, GetVersionExA, getVersionExA);
			HOOK_FUNCTION(kernel32, GetVersionExW, getVersionExW);
		}
	}
}
