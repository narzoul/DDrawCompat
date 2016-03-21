#define WIN32_LEAN_AND_MEAN

#include <Windows.h>
#include <detours.h>

#include "DDrawLog.h"
#include "Hook.h"

namespace
{
	FARPROC getProcAddress(HMODULE module, const char* procName)
	{
		if (!module || !procName)
		{
			return nullptr;
		}

		PIMAGE_DOS_HEADER dosHeader = reinterpret_cast<PIMAGE_DOS_HEADER>(module);
		if (IMAGE_DOS_SIGNATURE != dosHeader->e_magic) {
			return nullptr;
		}
		char* moduleBase = reinterpret_cast<char*>(module);

		PIMAGE_NT_HEADERS ntHeader = reinterpret_cast<PIMAGE_NT_HEADERS>(
			reinterpret_cast<char*>(dosHeader) + dosHeader->e_lfanew);
		if (IMAGE_NT_SIGNATURE != ntHeader->Signature)
		{
			return nullptr;
		}

		PIMAGE_EXPORT_DIRECTORY exportDir = reinterpret_cast<PIMAGE_EXPORT_DIRECTORY>(
			moduleBase + ntHeader->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT].VirtualAddress);

		DWORD* rvaOfNames = reinterpret_cast<DWORD*>(moduleBase + exportDir->AddressOfNames);

		for (DWORD i = 0; i < exportDir->NumberOfNames; ++i)
		{
			if (0 == strcmp(procName, moduleBase + rvaOfNames[i]))
			{
				WORD* nameOrds = reinterpret_cast<WORD*>(moduleBase + exportDir->AddressOfNameOrdinals);
				DWORD* rvaOfFunctions = reinterpret_cast<DWORD*>(moduleBase + exportDir->AddressOfFunctions);
				return reinterpret_cast<FARPROC>(moduleBase + rvaOfFunctions[nameOrds[i]]);
			}
		}

		return nullptr;
	}
}

namespace Compat
{
	void beginHookTransaction()
	{
		DetourTransactionBegin();
	}

	void endHookTransaction()
	{
		DetourTransactionCommit();
	}

	void hookFunction(void*& origFuncPtr, void* newFuncPtr)
	{
		DetourAttach(&origFuncPtr, newFuncPtr);
	}

	void hookFunction(const char* moduleName, const char* funcName, void*& origFuncPtr, void* newFuncPtr)
	{
		FARPROC procAddr = getProcAddress(GetModuleHandle(moduleName), funcName);
		if (!procAddr)
		{
			Compat::Log() << "Failed to load the address of a function: " << funcName;
			return;
		}

		origFuncPtr = procAddr;
		if (NO_ERROR != DetourAttach(&origFuncPtr, newFuncPtr))
		{
			Compat::Log() << "Failed to hook a function: " << funcName;
			return;
		}
	}
}