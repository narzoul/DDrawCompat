#include <Dll/Dll.h>

namespace Dll
{
	HMODULE g_currentModule = nullptr;
	Procs g_origProcs = {};
	Procs g_jmpTargetProcs = {};
}

#define CREATE_PROC_STUB(procName) \
	extern "C" __declspec(dllexport, naked) void procName() \
	{ \
		__asm jmp Dll::g_jmpTargetProcs.procName \
	}

VISIT_ALL_PROCS(CREATE_PROC_STUB)
