#include <process.h>

#include <Common/Hook.h>
#include <Dll/Dll.h>

namespace Dll
{
	HMODULE g_currentModule = nullptr;
	HMODULE g_origDDrawModule = nullptr;
	HMODULE g_origDciman32Module = nullptr;
	Procs g_origProcs = {};
	Procs g_jmpTargetProcs = {};
	bool g_isHooked = false;

	HANDLE createThread(unsigned(__stdcall* threadProc)(void*), unsigned int* threadId, int priority, unsigned initFlags)
	{
		HANDLE thread = reinterpret_cast<HANDLE>(_beginthreadex(nullptr, 0, threadProc, nullptr, initFlags, threadId));
		if (thread)
		{
			SetThreadPriority(thread, priority);
			CALL_ORIG_FUNC(SetThreadPriorityBoost)(thread, FALSE);
		}
		return thread;
	}

	std::string getEnvVar(const char* var)
	{
		const DWORD size = GetEnvironmentVariable(var, nullptr, 0);
		std::string value(size, 0);
		if (!value.empty())
		{
			GetEnvironmentVariable(var, &value.front(), size);
			value.pop_back();
		}
		return value;
	}

	void pinModule(HMODULE module)
	{
		HMODULE dummy = nullptr;
		GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
			reinterpret_cast<LPCSTR>(module), &dummy);
	}

	void pinModule(LPCSTR moduleName)
	{
		HMODULE module = nullptr;
		GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_PIN, moduleName, &module);
	}

	void pinModule(LPCWSTR moduleName)
	{
		HMODULE module = nullptr;
		GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_PIN, moduleName, &module);
	}
}

#define CREATE_PROC_STUB(procName) \
	extern "C" __declspec(dllexport, naked) void procName() \
	{ \
		__asm jmp Dll::g_jmpTargetProcs.procName \
	}

VISIT_ALL_PROCS(CREATE_PROC_STUB)
