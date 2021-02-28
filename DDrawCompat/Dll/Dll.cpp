#include <process.h>

#include <Dll/Dll.h>

namespace Dll
{
	HMODULE g_currentModule = nullptr;
	Procs g_origProcs = {};
	Procs g_jmpTargetProcs = {};

	HANDLE createThread(unsigned(__stdcall* threadProc)(void*), unsigned int* threadId, int priority)
	{
		HANDLE thread = reinterpret_cast<HANDLE>(_beginthreadex(nullptr, 0, threadProc, nullptr, 0, threadId));
		if (thread)
		{
			SetThreadPriority(thread, priority);
		}
		return thread;
	}
}

#define CREATE_PROC_STUB(procName) \
	extern "C" __declspec(dllexport, naked) void procName() \
	{ \
		__asm jmp Dll::g_jmpTargetProcs.procName \
	}

VISIT_ALL_PROCS(CREATE_PROC_STUB)
