#include <map>
#include <string>
#include <vector>

#include <Windows.h>

#include <Common/Hook.h>
#include <Common/Log.h>
#include <Common/ScopedCriticalSection.h>
#include <Dll/Dll.h>
#include <Win32/Registry.h>

typedef long NTSTATUS;

namespace Compat
{
	Log& operator<<(Log& os, HKEY hkey);
}

namespace
{
	struct RegValue
	{
		DWORD type;
		union {
			const wchar_t* str;
		};
	};

	struct RegSz : RegValue
	{
		RegSz(const wchar_t* value) : RegValue{ REG_SZ, value } {}
	};

	struct RegEntry
	{
		const wchar_t* keyName;
		const wchar_t* valueName;
		RegValue value;
	};

	template <auto origFunc>
	const char* g_funcName = nullptr;

	Compat::CriticalSection g_openKeysCs;
	std::map<HKEY, std::wstring> g_openKeys;

	const std::map<HKEY, std::wstring> g_predefinedKeys = {
#define PREDEFINED_KEY_NAME_PAIR(key) { key, L#key }
		PREDEFINED_KEY_NAME_PAIR(HKEY_CLASSES_ROOT),
		PREDEFINED_KEY_NAME_PAIR(HKEY_CURRENT_CONFIG),
		PREDEFINED_KEY_NAME_PAIR(HKEY_CURRENT_USER),
		PREDEFINED_KEY_NAME_PAIR(HKEY_CURRENT_USER_LOCAL_SETTINGS),
		PREDEFINED_KEY_NAME_PAIR(HKEY_DYN_DATA),
		PREDEFINED_KEY_NAME_PAIR(HKEY_LOCAL_MACHINE),
		PREDEFINED_KEY_NAME_PAIR(HKEY_PERFORMANCE_DATA),
		PREDEFINED_KEY_NAME_PAIR(HKEY_PERFORMANCE_NLSTEXT),
		PREDEFINED_KEY_NAME_PAIR(HKEY_PERFORMANCE_TEXT),
		PREDEFINED_KEY_NAME_PAIR(HKEY_USERS)
#undef  PREDEFINED_KEY_NAME_PAIR
	};

	const std::vector<RegEntry> g_regEntries = {
		{ L"HKEY_LOCAL_MACHINE\\Software\\Microsoft\\DirectDraw", L"EmulationOnly", {} },
		{ L"HKEY_LOCAL_MACHINE\\Software\\Microsoft\\Windows NT\\CurrentVersion\\DRIVERS32", L"vidc.iv31", RegSz(L"ir32_32.dll") },
		{ L"HKEY_LOCAL_MACHINE\\Software\\Microsoft\\Windows NT\\CurrentVersion\\DRIVERS32", L"vidc.iv41", RegSz(L"ir41_32.ax") },
		{ L"HKEY_LOCAL_MACHINE\\Software\\Microsoft\\Windows NT\\CurrentVersion\\DRIVERS32", L"vidc.iv50", RegSz(L"ir50_32.dll") },
	};

#undef HKLM_SOFTWARE_KEY

	LSTATUS WINAPI ddrawRegCreateKeyExA(HKEY hKey, LPCSTR lpSubKey, DWORD Reserved, LPSTR lpClass, DWORD dwOptions,
		REGSAM samDesired, const LPSECURITY_ATTRIBUTES lpSecurityAttributes, PHKEY phkResult, LPDWORD lpdwDisposition)
	{
		LOG_FUNC("ddrawRegCreateKeyExA", hKey, lpSubKey, Reserved, lpClass, dwOptions, samDesired,
			lpSecurityAttributes, phkResult, lpdwDisposition);
		if (0 == lstrcmpi(lpSubKey, "Software\\Microsoft\\Windows NT\\CurrentVersion\\AppCompatFlags\\Layers"))
		{
			return LOG_RESULT(E_ABORT);
		}
		return LOG_RESULT(RegCreateKeyExA(hKey, lpSubKey, Reserved, lpClass, dwOptions, samDesired,
			lpSecurityAttributes, phkResult, lpdwDisposition));
	}

	bool filterType(DWORD type, const DWORD* flags)
	{
		if (!flags)
		{
			return true;
		}

		switch (type)
		{
		case REG_SZ:
			return *flags & RRF_RT_REG_SZ;
		}

		return false;
	}

	template <typename... Params>
	HKEY* getHKeyPtr(HKEY* hkey, Params...)
	{
		return hkey;
	}

	template <typename FirstParam, typename... Params>
	HKEY* getHKeyPtr(FirstParam, Params... params)
	{
		return getHKeyPtr(params...);
	}

	std::wstring getKeyName(HKEY key)
	{
		if (!key)
		{
			return {};
		}

		auto it = g_predefinedKeys.find(key);
		if (it != g_predefinedKeys.end())
		{
			return it->second;
		}

		{
			Compat::ScopedCriticalSection lock(g_openKeysCs);
			it = g_openKeys.find(key);
			if (it != g_openKeys.end())
			{
				return it->second;
			}
		}

		enum KEY_INFORMATION_CLASS
		{
			KeyBasicInformation = 0,
			KeyNodeInformation = 1,
			KeyFullInformation = 2,
			KeyNameInformation = 3,
			KeyCachedInformation = 4,
			KeyFlagsInformation = 5,
			KeyVirtualizationInformation = 6,
			KeyHandleTagsInformation = 7,
			MaxKeyInfoClass = 8
		};

		typedef NTSTATUS(WINAPI* NtQueryKeyFuncPtr)(
			HANDLE KeyHandle,
			KEY_INFORMATION_CLASS KeyInformationClass,
			PVOID KeyInformation,
			ULONG Length,
			PULONG ResultLength);

		static NtQueryKeyFuncPtr ntQueryKey = reinterpret_cast<NtQueryKeyFuncPtr>(
			GetProcAddress(GetModuleHandle("ntdll"), "NtQueryKey"));

		if (ntQueryKey && key)
		{
			struct KEY_NAME_INFORMATION
			{
				ULONG NameLength;
				WCHAR Name[256];
			};

			KEY_NAME_INFORMATION keyName = {};
			ULONG resultSize = 0;
			if (SUCCEEDED(ntQueryKey(key, KeyNameInformation, &keyName, sizeof(keyName), &resultSize)))
			{
				return std::wstring(keyName.Name, keyName.NameLength / 2);
			}
		}
		
		return {};
	}

	std::size_t getLength(const char* str)
	{
		return strlen(str);
	}

	std::size_t getLength(const wchar_t* str)
	{
		return wcslen(str);
	}

	const RegValue* getValue(const std::wstring& keyName, const wchar_t* valueName)
	{
		if (!valueName)
		{
			valueName = L"";
		}
		for (const auto& regEntry : g_regEntries)
		{
			if (0 == lstrcmpiW(valueName, regEntry.valueName) &&
				0 == lstrcmpiW(keyName.c_str(), regEntry.keyName))
			{
				return &regEntry.value;
			}
		}
		return nullptr;
	}

	const RegValue* getValue(const std::wstring& keyName, const char* valueName)
	{
		if (!valueName)
		{
			return getValue(keyName, L"");
		}
		std::wstring convertedValueName(valueName, valueName + strlen(valueName));
		return getValue(keyName, convertedValueName.c_str());
	}

	template <typename Char>
	LONG getValue(HKEY hkey, const Char* subKeyName, const Char* valueName,
		const DWORD* flags, DWORD* type, void* data, DWORD* length)
	{
		if (data && !length)
		{
			return -1;
		}

		auto keyName(getKeyName(hkey));
		if (keyName.empty())
		{
			return -1;
		}

		if (subKeyName)
		{
			keyName += L'\\';
			keyName.append(subKeyName, subKeyName + getLength(subKeyName));
		}

		const RegValue* value = getValue(keyName, valueName);
		if (!value)
		{
			return -1;
		}

		if (REG_NONE == value->type)
		{
			return ERROR_FILE_NOT_FOUND;
		}

		if (!filterType(value->type, flags))
		{
			return -1;
		}

		if (type)
		{
			*type = value->type;
		}

		if (!length)
		{
			return ERROR_SUCCESS;
		}

		const void* src = nullptr;
		const DWORD maxLength = *length;

		switch (value->type)
		{
		case REG_SZ:
			src = value->str;
			*length = (getLength(value->str) + 1) * sizeof(Char);
			break;

		default:
			*length = 0;
			break;
		}

		if (data)
		{
			if (*length > maxLength)
			{
				if (flags && (*flags & RRF_ZEROONFAILURE))
				{
					memset(data, 0, *length);
				}
				return ERROR_MORE_DATA;
			}

			memcpy(data, src, *length);
		}

		return ERROR_SUCCESS;
	}

	void hookRegistryFunction(void*& origFuncPtr, void* newFuncPtr, const char* funcName)
	{
		auto kernelBase = LoadLibrary("kernelbase");
		if (kernelBase && Compat::getProcAddress(kernelBase, funcName))
		{
			Compat::hookFunction(kernelBase, funcName, origFuncPtr, newFuncPtr);
		}
		else
		{
			Compat::hookFunction("advapi32", funcName, origFuncPtr, newFuncPtr);
		}
	}

	LSTATUS WINAPI regCloseKey(HKEY hKey)
	{
		LOG_FUNC("RegCloseKey", hKey);
		const auto result = CALL_ORIG_FUNC(RegCloseKey)(hKey);
		if (ERROR_SUCCESS == result)
		{
			Compat::ScopedCriticalSection lock(g_openKeysCs);
			g_openKeys.erase(hKey);
		}
		return LOG_RESULT(result);
	}

	template <typename Char, typename OrigFunc>
	LONG regGetValue(HKEY hkey, const Char* lpSubKey, const Char* lpValue,
		DWORD dwFlags, LPDWORD pdwType, PVOID pvData, LPDWORD pcbData, OrigFunc origFunc, const char* funcName)
	{
		LOG_FUNC(funcName, hkey, lpSubKey, lpValue, dwFlags, pdwType, pvData, pcbData);
		const auto result = getValue(hkey, lpSubKey, lpValue, &dwFlags, pdwType, pvData, pcbData);
		if (-1 != result)
		{
			return LOG_RESULT(result);
		}
		return LOG_RESULT(origFunc(hkey, lpSubKey, lpValue, dwFlags, pdwType, pvData, pcbData));
	}

	LONG WINAPI regGetValueA(HKEY hkey, LPCSTR lpSubKey, LPCSTR lpValue,
		DWORD dwFlags, LPDWORD pdwType, PVOID pvData, LPDWORD pcbData)
	{
		return regGetValue(hkey, lpSubKey, lpValue, dwFlags, pdwType, pvData, pcbData,
			CALL_ORIG_FUNC(RegGetValueA), "RegGetValueA");
	}

	LONG WINAPI regGetValueW(HKEY hkey, LPCWSTR lpSubKey, LPCWSTR lpValue,
		DWORD dwFlags, LPDWORD pdwType, PVOID pvData, LPDWORD pcbData)
	{
		return regGetValue(hkey, lpSubKey, lpValue, dwFlags, pdwType, pvData, pcbData,
			CALL_ORIG_FUNC(RegGetValueW), "RegGetValueW");
	}

	template <auto origFunc, typename Char, typename... Params>
	LSTATUS WINAPI regOpenKey(HKEY hKey, const Char* lpSubKey, Params... params)
	{
		LOG_FUNC(g_funcName<origFunc>, hKey, lpSubKey, params...);
		const auto result = Compat::g_origFuncPtr<origFunc>(hKey, lpSubKey, params...);
		if (ERROR_SUCCESS == result)
		{
			const auto hkeyPtr = getHKeyPtr(params...);
			if (hkeyPtr)
			{
				auto keyName(getKeyName(hKey));
				if (lpSubKey)
				{
					keyName += L'\\';
					keyName.append(lpSubKey, lpSubKey + getLength(lpSubKey));
				}

				Compat::ScopedCriticalSection lock(g_openKeysCs);
				g_openKeys[*hkeyPtr] = keyName;
			}
		}
		return LOG_RESULT(result);
	}

	template <typename Char, typename OrigFunc>
	LONG regQueryValueEx(HKEY hKey, const Char* lpValueName, LPDWORD lpReserved, LPDWORD lpType,
		LPBYTE lpData, LPDWORD lpcbData, OrigFunc origFunc, const char* funcName)
	{
		LOG_FUNC(funcName, hKey, lpValueName, lpReserved, lpType, static_cast<void*>(lpData), lpcbData);
		const auto result = getValue<Char>(hKey, nullptr, lpValueName, nullptr, lpType, lpData, lpcbData);
		if (-1 != result)
		{
			return LOG_RESULT(result);
		}
		return LOG_RESULT(origFunc(hKey, lpValueName, lpReserved, lpType, lpData, lpcbData));
	}

	LONG WINAPI regQueryValueExA(HKEY hKey, LPCSTR lpValueName, LPDWORD lpReserved, LPDWORD lpType,
		LPBYTE lpData, LPDWORD lpcbData)
	{
		return regQueryValueEx(hKey, lpValueName, lpReserved, lpType, lpData, lpcbData,
			CALL_ORIG_FUNC(RegQueryValueExA), "RegQueryValueExA");
	}

	LONG WINAPI regQueryValueExW(HKEY hKey, LPCWSTR lpValueName, LPDWORD lpReserved, LPDWORD lpType,
		LPBYTE lpData, LPDWORD lpcbData)
	{
		return regQueryValueEx(hKey, lpValueName, lpReserved, lpType, lpData, lpcbData,
			CALL_ORIG_FUNC(RegQueryValueExW), "RegQueryValueExW");
	}
}

namespace Compat
{
	Log& operator<<(Log& os, HKEY hkey)
	{
		auto it = g_predefinedKeys.find(hkey);
		if (it != g_predefinedKeys.end())
		{
			return os << it->second.c_str();
		}

		os << "HKEY(" << static_cast<void*>(hkey);
		auto keyName(getKeyName(hkey));
		if (!keyName.empty())
		{
			os << ',' << '"' << keyName.c_str() << '"';
		}
		return os << ')';
	}
}

#define HOOK_REGISTRY_FUNCTION(func, newFunc) \
	hookRegistryFunction(reinterpret_cast<void*&>(Compat::g_origFuncPtr<&func>), static_cast<decltype(&func)>(newFunc), #func)

#define HOOK_REGISTRY_OPEN_FUNCTION(func) \
	g_funcName<func> = #func; \
	HOOK_REGISTRY_FUNCTION(func, regOpenKey<func>)

namespace Win32
{
	namespace Registry
	{
		void installHooks()
		{
			HOOK_REGISTRY_OPEN_FUNCTION(RegCreateKeyA);
			HOOK_REGISTRY_OPEN_FUNCTION(RegCreateKeyW);
			HOOK_REGISTRY_OPEN_FUNCTION(RegCreateKeyExA);
			HOOK_REGISTRY_OPEN_FUNCTION(RegCreateKeyExW);
			HOOK_REGISTRY_OPEN_FUNCTION(RegCreateKeyTransactedA);
			HOOK_REGISTRY_OPEN_FUNCTION(RegCreateKeyTransactedW);

			HOOK_REGISTRY_OPEN_FUNCTION(RegOpenKeyA);
			HOOK_REGISTRY_OPEN_FUNCTION(RegOpenKeyW);
			HOOK_REGISTRY_OPEN_FUNCTION(RegOpenKeyExA);
			HOOK_REGISTRY_OPEN_FUNCTION(RegOpenKeyExW);
			HOOK_REGISTRY_OPEN_FUNCTION(RegOpenKeyTransactedA);
			HOOK_REGISTRY_OPEN_FUNCTION(RegOpenKeyTransactedW);

			HOOK_REGISTRY_FUNCTION(RegCloseKey, regCloseKey);
			HOOK_REGISTRY_FUNCTION(RegGetValueA, regGetValueA);
			HOOK_REGISTRY_FUNCTION(RegGetValueW, regGetValueW);
			HOOK_REGISTRY_FUNCTION(RegQueryValueExA, regQueryValueExA);
			HOOK_REGISTRY_FUNCTION(RegQueryValueExW, regQueryValueExW);

			Compat::hookIatFunction(Dll::g_origDDrawModule, "RegCreateKeyExA", ddrawRegCreateKeyExA);
		}
	}
}
