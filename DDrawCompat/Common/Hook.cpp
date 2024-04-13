#undef CINTERFACE

#include <list>
#include <sstream>
#include <string>

#include <Windows.h>
#include <initguid.h>
#include <DbgEng.h>

#include <Common/Hook.h>
#include <Common/Log.h>
#include <Common/Path.h>
#include <Dll/Dll.h>

namespace
{
	IDebugClient4* g_debugClient = nullptr;
	IDebugControl* g_debugControl = nullptr;
	IDebugSymbols* g_debugSymbols = nullptr;
	IDebugDataSpaces4* g_debugDataSpaces = nullptr;
	ULONG64 g_debugBase = 0;
	bool g_isDbgEngInitialized = false;

	LONG WINAPI dbgEngWinVerifyTrust(HWND hwnd, GUID* pgActionID, LPVOID pWVTData);
	PIMAGE_NT_HEADERS getImageNtHeaders(HMODULE module);
	bool initDbgEng();

	FARPROC WINAPI dbgEngGetProcAddress(HMODULE hModule, LPCSTR lpProcName)
	{
		LOG_FUNC("dbgEngGetProcAddress", hModule, lpProcName);
		if (0 == strcmp(lpProcName, "WinVerifyTrust"))
		{
			return LOG_RESULT(reinterpret_cast<FARPROC>(&dbgEngWinVerifyTrust));
		}
		return LOG_RESULT(GetProcAddress(hModule, lpProcName));
	}

	LONG WINAPI dbgEngWinVerifyTrust(
		[[maybe_unused]] HWND hwnd,
		[[maybe_unused]] GUID* pgActionID,
		[[maybe_unused]] LPVOID pWVTData)
	{
		LOG_FUNC("dbgEngWinVerifyTrust", hwnd, pgActionID, pWVTData);
		return LOG_RESULT(0);
	}

	FARPROC* findProcAddressInIat(HMODULE module, const char* procName)
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
		PIMAGE_IMPORT_DESCRIPTOR importDesc = reinterpret_cast<PIMAGE_IMPORT_DESCRIPTOR>(moduleBase +
			ntHeaders->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress);

		for (PIMAGE_IMPORT_DESCRIPTOR desc = importDesc;
			0 != desc->Characteristics && 0xFFFF != desc->Name;
			++desc)
		{
			auto thunk = reinterpret_cast<PIMAGE_THUNK_DATA>(moduleBase + desc->FirstThunk);
			auto origThunk = reinterpret_cast<PIMAGE_THUNK_DATA>(moduleBase + desc->OriginalFirstThunk);
			while (0 != thunk->u1.AddressOfData && 0 != origThunk->u1.AddressOfData)
			{
				if (!(origThunk->u1.Ordinal & IMAGE_ORDINAL_FLAG))
				{
					auto origImport = reinterpret_cast<PIMAGE_IMPORT_BY_NAME>(moduleBase + origThunk->u1.AddressOfData);
					if (0 == strcmp(origImport->Name, procName))
					{
						return reinterpret_cast<FARPROC*>(&thunk->u1.Function);
					}
				}

				++thunk;
				++origThunk;
			}
		}

		return nullptr;
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

	void hookFunction(void*& origFuncPtr, void* newFuncPtr, const char* funcName)
	{
		BYTE* targetFunc = static_cast<BYTE*>(origFuncPtr);

		std::ostringstream oss;
		oss << Compat::funcPtrToStr(targetFunc) << ' ';

		char origFuncPtrStr[20] = {};
		if (!funcName)
		{
			sprintf_s(origFuncPtrStr, "%p", origFuncPtr);
			funcName = origFuncPtrStr;
		}

		auto prevTargetFunc = targetFunc;
		while (true)
		{
			unsigned instructionSize = 0;
			if (0xE9 == targetFunc[0])
			{
				instructionSize = 5;
				targetFunc += instructionSize + *reinterpret_cast<int*>(targetFunc + 1);
			}
			else if (0xEB == targetFunc[0])
			{
				instructionSize = 2;
				targetFunc += instructionSize + *reinterpret_cast<signed char*>(targetFunc + 1);
			}
			else if (0xFF == targetFunc[0] && 0x25 == targetFunc[1])
			{
				instructionSize = 6;
				targetFunc = **reinterpret_cast<BYTE***>(targetFunc + 2);
				if (Compat::getModuleHandleFromAddress(targetFunc) == Compat::getModuleHandleFromAddress(prevTargetFunc))
				{
					targetFunc = prevTargetFunc;
					break;
				}
			}
			else
			{
				break;
			}

			Compat::LogStream(oss) << Compat::hexDump(prevTargetFunc, instructionSize) << " -> "
				<< Compat::funcPtrToStr(targetFunc) << ' ';
			prevTargetFunc = targetFunc;
		}

		if (Compat::getModuleHandleFromAddress(targetFunc) == Dll::g_currentModule)
		{
			LOG_INFO << "ERROR: Target function is already hooked: " << funcName;
			return;
		}

		if (!initDbgEng())
		{
			return;
		}

		const DWORD trampolineSize = 32;
		BYTE* trampoline = static_cast<BYTE*>(
			VirtualAlloc(nullptr, trampolineSize, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
		BYTE* src = targetFunc;
		BYTE* dst = trampoline;
		while (src - targetFunc < 5)
		{
			unsigned instructionSize = Compat::getInstructionSize(src);
			if (0 == instructionSize)
			{
				return;
			}

			memcpy(dst, src, instructionSize);
			if (0xE8 == *src && 5 == instructionSize)
			{
				*reinterpret_cast<int*>(dst + 1) += src - dst;
			}

			src += instructionSize;
			dst += instructionSize;
		}

		LOG_DEBUG << "Hooking function: " << funcName
			<< " (" << oss.str() << Compat::hexDump(targetFunc, src - targetFunc) << ')';

		*dst = 0xE9;
		*reinterpret_cast<int*>(dst + 1) = src - (dst + 5);
		DWORD oldProtect = 0;
		VirtualProtect(trampoline, trampolineSize, PAGE_EXECUTE_READ, &oldProtect);

		VirtualProtect(targetFunc, src - targetFunc, PAGE_EXECUTE_READWRITE, &oldProtect);
		targetFunc[0] = 0xE9;
		*reinterpret_cast<int*>(targetFunc + 1) = static_cast<BYTE*>(newFuncPtr) - (targetFunc + 5);
		memset(targetFunc + 5, 0xCC, src - targetFunc - 5);
		VirtualProtect(targetFunc, src - targetFunc, PAGE_EXECUTE_READ, &oldProtect);

		origFuncPtr = trampoline;
		CALL_ORIG_FUNC(FlushInstructionCache)(GetCurrentProcess(), nullptr, 0);

		HMODULE module = nullptr;
		GetModuleHandleEx(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
			reinterpret_cast<char*>(targetFunc), &module);
	}

	bool initDbgEng()
	{
		if (g_isDbgEngInitialized)
		{
			return 0 != g_debugBase;
		}
		g_isDbgEngInitialized = true;

		if (!GetModuleHandle("dbghelp.dll"))
		{
			LoadLibraryW((Compat::getSystemPath() / "dbghelp.dll").c_str());
		}

		auto dbgEng = LoadLibraryW((Compat::getSystemPath() / "dbgeng.dll").c_str());
		if (!dbgEng)
		{
			LOG_INFO << "ERROR: DbgEng: failed to load library";
			return false;
		}

		Compat::hookIatFunction(dbgEng, "GetProcAddress", dbgEngGetProcAddress);

		auto debugCreate = reinterpret_cast<decltype(&DebugCreate)>(Compat::getProcAddress(dbgEng, "DebugCreate"));
		if (!debugCreate)
		{
			LOG_INFO << "ERROR: DbgEng: DebugCreate not found";
			return false;
		}

		HRESULT result = S_OK;
		if (FAILED(result = debugCreate(IID_IDebugClient4, reinterpret_cast<void**>(&g_debugClient))) ||
			FAILED(result = g_debugClient->QueryInterface(IID_IDebugControl, reinterpret_cast<void**>(&g_debugControl))) ||
			FAILED(result = g_debugClient->QueryInterface(IID_IDebugSymbols, reinterpret_cast<void**>(&g_debugSymbols))) ||
			FAILED(result = g_debugClient->QueryInterface(IID_IDebugDataSpaces4, reinterpret_cast<void**>(&g_debugDataSpaces))))
		{
			LOG_INFO << "ERROR: DbgEng: object creation failed: " << Compat::hex(result);
			return false;
		}

		result = g_debugClient->OpenDumpFileWide(Compat::getModulePath(Dll::g_currentModule).c_str(), 0);
		if (FAILED(result))
		{
			LOG_INFO << "ERROR: DbgEng: OpenDumpFile failed: " << Compat::hex(result);
			return false;
		}

		g_debugControl->SetEngineOptions(DEBUG_ENGOPT_DISABLE_MODULE_SYMBOL_LOAD);
		result = g_debugControl->WaitForEvent(0, INFINITE);
		if (FAILED(result))
		{
			LOG_INFO << "ERROR: DbgEng: WaitForEvent failed: " << Compat::hex(result);
			return false;
		}

		DEBUG_MODULE_PARAMETERS dmp = {};
		result = g_debugSymbols->GetModuleParameters(1, 0, 0, &dmp);
		if (FAILED(result))
		{
			LOG_INFO << "ERROR: DbgEng: GetModuleParameters failed: " << Compat::hex(result);
			return false;
		}

		ULONG size = 0;
		result = g_debugDataSpaces->GetValidRegionVirtual(dmp.Base, dmp.Size, &g_debugBase, &size);
		if (FAILED(result) || 0 == g_debugBase)
		{
			LOG_INFO << "ERROR: DbgEng: GetValidRegionVirtual failed: " << Compat::hex(result);
			return false;
		}

		return true;
	}
}

namespace Compat
{
	void closeDbgEng()
	{
		if (g_debugClient)
		{
			g_debugClient->EndSession(DEBUG_END_PASSIVE);
		}
		if (g_debugDataSpaces)
		{
			g_debugDataSpaces->Release();
			g_debugDataSpaces = nullptr;
		}
		if (g_debugSymbols)
		{
			g_debugSymbols->Release();
			g_debugSymbols = nullptr;
		}
		if (g_debugControl)
		{
			g_debugControl->Release();
			g_debugControl = nullptr;
		}
		if (g_debugClient)
		{
			g_debugClient->Release();
			g_debugClient = nullptr;
		}

		g_debugBase = 0;
		g_isDbgEngInitialized = false;
	}

	std::string funcPtrToStr(const void* funcPtr)
	{
		std::ostringstream oss;
		HMODULE module = Compat::getModuleHandleFromAddress(funcPtr);
		if (module)
		{
			oss << Compat::getModulePath(module).u8string() << "+0x" << std::hex <<
				reinterpret_cast<DWORD>(funcPtr) - reinterpret_cast<DWORD>(module);
		}
		else
		{
			oss << funcPtr;
		}
		return oss.str();
	}

	unsigned getInstructionSize(void* instruction)
	{
		const unsigned MAX_INSTRUCTION_SIZE = 15;
		HRESULT result = g_debugDataSpaces->WriteVirtual(g_debugBase, instruction, MAX_INSTRUCTION_SIZE, nullptr);
		if (FAILED(result))
		{
			LOG_ONCE("ERROR: DbgEng: WriteVirtual failed: " << Compat::hex(result));
			return 0;
		}

		ULONG64 endOffset = 0;
		result = g_debugControl->Disassemble(g_debugBase, 0, nullptr, 0, nullptr, &endOffset);
		if (FAILED(result))
		{
			LOG_ONCE("ERROR: DbgEng: Disassemble failed: " << Compat::hex(result) << " "
				<< Compat::hexDump(instruction, MAX_INSTRUCTION_SIZE));
			return 0;
		}

		return static_cast<unsigned>(endOffset - g_debugBase);
	}

	DWORD getModuleFileOffset(const void* address)
	{
		LOG_FUNC("getModuleFileOffset", address);
		HMODULE mod = getModuleHandleFromAddress(address);
		if (!mod)
		{
			return LOG_RESULT(0);
		}

		PIMAGE_NT_HEADERS ntHeaders = getImageNtHeaders(mod);
		if (!ntHeaders)
		{
			return LOG_RESULT(0);
		}

		DWORD offset = static_cast<const BYTE*>(address) - reinterpret_cast<const BYTE*>(mod);
		auto sectionHeader =  reinterpret_cast<IMAGE_SECTION_HEADER*>(
			&ntHeaders->OptionalHeader.DataDirectory[ntHeaders->OptionalHeader.NumberOfRvaAndSizes]);
		for (unsigned i = 0; i < ntHeaders->FileHeader.NumberOfSections; ++i)
		{
			if (offset >= sectionHeader->VirtualAddress &&
				offset < sectionHeader->VirtualAddress + sectionHeader->SizeOfRawData)
			{
				return LOG_RESULT(sectionHeader->PointerToRawData + offset - sectionHeader->VirtualAddress);
			}
			sectionHeader++;
		}
		return LOG_RESULT(0);
	}

	HMODULE getModuleHandleFromAddress(const void* address)
	{
		HMODULE module = nullptr;
		GetModuleHandleEx(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
			static_cast<const char*>(address), &module);
		return module;
	}

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

	void hookIatFunction(HMODULE module, const char* funcName, void* newFuncPtr)
	{
		FARPROC* func = findProcAddressInIat(module, funcName);
		if (func)
		{
			LOG_DEBUG << "Hooking function via IAT: " << funcName << " (" << funcPtrToStr(*func) << ')';
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
}
