#include <algorithm>
#include <filesystem>
#include <list>
#include <map>
#include <sstream>
#include <string>
#include <utility>

#include <Windows.h>
#include <detours.h>

#include <Common/Hook.h>
#include <Common/Log.h>

namespace
{
	struct HookedFunctionInfo
	{
		HMODULE module;
		void*& origFunction;
		void* newFunction;
	};

	std::map<void*, HookedFunctionInfo> g_hookedFunctions;

	PIMAGE_NT_HEADERS getImageNtHeaders(HMODULE module);
	HMODULE getModuleHandleFromAddress(void* address);
	std::filesystem::path getModulePath(HMODULE module);

	std::map<void*, HookedFunctionInfo>::iterator findOrigFunc(void* origFunc)
	{
		return std::find_if(g_hookedFunctions.begin(), g_hookedFunctions.end(),
			[=](const auto& i) { return origFunc == i.first || origFunc == i.second.origFunction; });
	}

	FARPROC* findProcAddressInIat(HMODULE module, const char* importedModuleName, const char* procName)
	{
		if (!module || !importedModuleName || !procName)
		{
			return nullptr;
		}

		PIMAGE_NT_HEADERS ntHeaders = getImageNtHeaders(module);
		if (!ntHeaders)
		{
			return nullptr;
		}

		char* moduleBase = reinterpret_cast<char*>(module);
		PIMAGE_IMPORT_DESCRIPTOR importDesc = reinterpret_cast<PIMAGE_IMPORT_DESCRIPTOR>(moduleBase +
			ntHeaders->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress);

		for (PIMAGE_IMPORT_DESCRIPTOR desc = importDesc;
			0 != desc->Characteristics && 0xFFFF != desc->Name;
			++desc)
		{
			if (0 != _stricmp(moduleBase + desc->Name, importedModuleName))
			{
				continue;
			}

			auto thunk = reinterpret_cast<PIMAGE_THUNK_DATA>(moduleBase + desc->FirstThunk);
			auto origThunk = reinterpret_cast<PIMAGE_THUNK_DATA>(moduleBase + desc->OriginalFirstThunk);
			while (0 != thunk->u1.AddressOfData && 0 != origThunk->u1.AddressOfData)
			{
				auto origImport = reinterpret_cast<PIMAGE_IMPORT_BY_NAME>(
					moduleBase + origThunk->u1.AddressOfData);

				if (0 == strcmp(origImport->Name, procName))
				{
					return reinterpret_cast<FARPROC*>(&thunk->u1.Function);
				}

				++thunk;
				++origThunk;
			}

			break;
		}

		return nullptr;
	}

	std::string funcAddrToStr(void* funcPtr)
	{
		std::ostringstream oss;
		HMODULE module = getModuleHandleFromAddress(funcPtr);
		oss << getModulePath(module).string() << "+0x" << std::hex <<
			reinterpret_cast<DWORD>(funcPtr) - reinterpret_cast<DWORD>(module);
		return oss.str();
	}

	PIMAGE_NT_HEADERS getImageNtHeaders(HMODULE module)
	{
		PIMAGE_DOS_HEADER dosHeader = reinterpret_cast<PIMAGE_DOS_HEADER>(module);
		if (IMAGE_DOS_SIGNATURE != dosHeader->e_magic)
		{
			return nullptr;
		}

		PIMAGE_NT_HEADERS ntHeaders = reinterpret_cast<PIMAGE_NT_HEADERS>(
			reinterpret_cast<char*>(dosHeader) + dosHeader->e_lfanew);
		if (IMAGE_NT_SIGNATURE != ntHeaders->Signature)
		{
			return nullptr;
		}

		return ntHeaders;
	}

	HMODULE getModuleHandleFromAddress(void* address)
	{
		HMODULE module = nullptr;
		GetModuleHandleEx(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
			static_cast<char*>(address), &module);
		return module;
	}

	std::filesystem::path getModulePath(HMODULE module)
	{
		char path[MAX_PATH] = {};
		GetModuleFileName(module, path, sizeof(path));
		return path;
	}

	void hookFunction(void*& origFuncPtr, void* newFuncPtr, const char* funcName)
	{
		const auto it = findOrigFunc(origFuncPtr);
		if (it != g_hookedFunctions.end())
		{
			origFuncPtr = it->second.origFunction;
			return;
		}

		char origFuncPtrStr[20] = {};
		if (!funcName)
		{
			sprintf_s(origFuncPtrStr, "%p", origFuncPtr);
			funcName = origFuncPtrStr;
		}

		void* const hookedFuncPtr = origFuncPtr;
		HMODULE module = nullptr;
		GetModuleHandleEx(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
			static_cast<char*>(hookedFuncPtr), &module);

		LOG_DEBUG << "Hooking function: " << funcName << " (" << funcAddrToStr(hookedFuncPtr) << ')';

		DetourTransactionBegin();
		const bool attachSuccessful = NO_ERROR == DetourAttach(&origFuncPtr, newFuncPtr);
		const bool commitSuccessful = NO_ERROR == DetourTransactionCommit();
		if (!attachSuccessful || !commitSuccessful)
		{
			LOG_DEBUG << "ERROR: Failed to hook a function: " << funcName;
			return;
		}

		g_hookedFunctions.emplace(
			std::make_pair(hookedFuncPtr, HookedFunctionInfo{ module, origFuncPtr, newFuncPtr }));
	}

	void unhookFunction(const std::map<void*, HookedFunctionInfo>::iterator& hookedFunc)
	{
		DetourTransactionBegin();
		DetourDetach(&hookedFunc->second.origFunction, hookedFunc->second.newFunction);
		DetourTransactionCommit();

		if (hookedFunc->second.module)
		{
			FreeLibrary(hookedFunc->second.module);
		}
		g_hookedFunctions.erase(hookedFunc);
	}
}

namespace Compat
{
	FARPROC getProcAddress(HMODULE module, const char* procName)
	{
		if (!module || !procName)
		{
			return nullptr;
		}

		PIMAGE_NT_HEADERS ntHeaders = getImageNtHeaders(module);
		if (!ntHeaders)
		{
			return nullptr;
		}

		char* moduleBase = reinterpret_cast<char*>(module);
		PIMAGE_EXPORT_DIRECTORY exportDir = reinterpret_cast<PIMAGE_EXPORT_DIRECTORY>(
			moduleBase + ntHeaders->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT].VirtualAddress);
		auto exportDirSize = ntHeaders->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT].Size;

		DWORD* rvaOfNames = reinterpret_cast<DWORD*>(moduleBase + exportDir->AddressOfNames);
		WORD* nameOrds = reinterpret_cast<WORD*>(moduleBase + exportDir->AddressOfNameOrdinals);
		DWORD* rvaOfFunctions = reinterpret_cast<DWORD*>(moduleBase + exportDir->AddressOfFunctions);

		char* func = nullptr;
		if (0 == HIWORD(procName))
		{
			WORD ord = LOWORD(procName);
			if (ord < exportDir->Base || ord >= exportDir->Base + exportDir->NumberOfFunctions)
			{
				return nullptr;
			}
			func = moduleBase + rvaOfFunctions[ord - exportDir->Base];
		}
		else
		{
			for (DWORD i = 0; i < exportDir->NumberOfNames; ++i)
			{
				if (0 == strcmp(procName, moduleBase + rvaOfNames[i]))
				{
					func = moduleBase + rvaOfFunctions[nameOrds[i]];
				}
			}
		}

		if (func &&
			func >= reinterpret_cast<char*>(exportDir) &&
			func < reinterpret_cast<char*>(exportDir) + exportDirSize)
		{
			std::string forw(func);
			auto separatorPos = forw.find_first_of('.');
			if (std::string::npos == separatorPos)
			{
				return nullptr;
			}
			HMODULE forwModule = GetModuleHandle(forw.substr(0, separatorPos).c_str());
			std::string forwFuncName = forw.substr(separatorPos + 1);
			if ('#' == forwFuncName[0])
			{
				int32_t ord = std::atoi(forwFuncName.substr(1).c_str());
				if (ord < 0 || ord > 0xFFFF)
				{
					return nullptr;
				}
				return getProcAddress(forwModule, reinterpret_cast<const char*>(ord));
			}
			else
			{
				return getProcAddress(forwModule, forwFuncName.c_str());
			}
		}

		// Avoid hooking ntdll stubs (e.g. ntdll/NtdllDialogWndProc_A instead of user32/DefDlgProcA)
		if (func && getModuleHandleFromAddress(func) != module &&
			0xFF == static_cast<BYTE>(func[0]) &&
			0x25 == static_cast<BYTE>(func[1]))
		{
			FARPROC jmpTarget = **reinterpret_cast<FARPROC**>(func + 2);
			if (getModuleHandleFromAddress(jmpTarget) == module)
			{
				return jmpTarget;
			}
		}

		return reinterpret_cast<FARPROC>(func);
	}

	void hookFunction(void*& origFuncPtr, void* newFuncPtr, const char* funcName)
	{
		::hookFunction(origFuncPtr, newFuncPtr, funcName);
	}

	void hookFunction(HMODULE module, const char* funcName, void*& origFuncPtr, void* newFuncPtr)
	{
		FARPROC procAddr = getProcAddress(module, funcName);
		if (!procAddr)
		{
			LOG_DEBUG << "ERROR: Failed to load the address of a function: " << funcName;
			return;
		}

		origFuncPtr = procAddr;
		::hookFunction(origFuncPtr, newFuncPtr, funcName);
	}

	void hookFunction(const char* moduleName, const char* funcName, void*& origFuncPtr, void* newFuncPtr)
	{
		HMODULE module = LoadLibrary(moduleName);
		if (!module)
		{
			return;
		}
		hookFunction(module, funcName, origFuncPtr, newFuncPtr);
		FreeLibrary(module);
	}

	void hookIatFunction(HMODULE module, const char* importedModuleName, const char* funcName, void* newFuncPtr)
	{
		FARPROC* func = findProcAddressInIat(module, importedModuleName, funcName);
		if (func)
		{
			LOG_DEBUG << "Hooking function via IAT: " << funcName << " (" << funcAddrToStr(*func) << ')';
			DWORD oldProtect = 0;
			VirtualProtect(func, sizeof(func), PAGE_READWRITE, &oldProtect);
			*func = static_cast<FARPROC>(newFuncPtr);
			DWORD dummy = 0;
			VirtualProtect(func, sizeof(func), oldProtect, &dummy);
		}
	}

	void removeShim(HMODULE module, const char* funcName)
	{
		void* shimFunc = GetProcAddress(module, funcName);
		if (shimFunc)
		{
			void* realFunc = getProcAddress(module, funcName);
			if (realFunc && shimFunc != realFunc)
			{
				static std::list<void*> shimFuncs;
				shimFuncs.push_back(shimFunc);
				std::string shimFuncName("[shim]");
				shimFuncName += funcName;
				hookFunction(shimFuncs.back(), realFunc, shimFuncName.c_str());
			}
		}
	}

	void unhookAllFunctions()
	{
		while (!g_hookedFunctions.empty())
		{
			::unhookFunction(g_hookedFunctions.begin());
		}
	}

	void unhookFunction(void* origFunc)
	{
		auto it = findOrigFunc(origFunc);
		if (it != g_hookedFunctions.end())
		{
			::unhookFunction(it);
		}
	}
}
