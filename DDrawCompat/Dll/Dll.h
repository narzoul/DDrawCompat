#pragma once

#include <string>

#include <Windows.h>

#define VISIT_PUBLIC_DDRAW_PROCS(visit) \
	visit(DirectDrawCreate) \
	visit(DirectDrawCreateClipper) \
	visit(DirectDrawCreateEx) \
	visit(DirectDrawEnumerateA) \
	visit(DirectDrawEnumerateExA) \
	visit(DirectDrawEnumerateExW) \
	visit(DirectDrawEnumerateW) \
	visit(DllGetClassObject)

#define VISIT_PRIVATE_DDRAW_PROCS(visit) \
	visit(AcquireDDThreadLock) \
	visit(CompleteCreateSysmemSurface) \
	visit(D3DParseUnknownCommand) \
	visit(DDGetAttachedSurfaceLcl) \
	visit(DDInternalLock) \
	visit(DDInternalUnlock) \
	visit(DSoundHelp) \
	visit(DllCanUnloadNow) \
	visit(GetDDSurfaceLocal) \
	visit(GetOLEThunkData) \
	visit(GetSurfaceFromDC) \
	visit(RegisterSpecialCase) \
	visit(ReleaseDDThreadLock) \
	visit(SetAppCompatData)

#define VISIT_DDRAW_PROCS(visit) \
	VISIT_PUBLIC_DDRAW_PROCS(visit) \
	VISIT_PRIVATE_DDRAW_PROCS(visit)

#define VISIT_DCIMAN32_PROCS(visit) \
	visit(DCIBeginAccess) \
	visit(DCICloseProvider) \
	visit(DCICreateOffscreen) \
	visit(DCICreateOverlay) \
	visit(DCICreatePrimary) \
	visit(DCIDestroy) \
	visit(DCIDraw) \
	visit(DCIEndAccess) \
	visit(DCIEnum) \
	visit(DCIOpenProvider) \
	visit(DCISetClipList) \
	visit(DCISetDestination) \
	visit(DCISetSrcDestClip) \
	visit(GetDCRegionData) \
	visit(GetWindowRegionData) \
	visit(WinWatchClose) \
	visit(WinWatchDidStatusChange) \
	visit(WinWatchGetClipList) \
	visit(WinWatchNotify) \
	visit(WinWatchOpen)

#define VISIT_ALL_PROCS(visit) \
	VISIT_DDRAW_PROCS(visit) \
	VISIT_DCIMAN32_PROCS(visit)

namespace Dll
{
	struct Procs
	{
#define ADD_FARPROC_MEMBER(memberName) FARPROC memberName;
		VISIT_ALL_PROCS(ADD_FARPROC_MEMBER);
#undef  ADD_FARPROC_MEMBER
	};

	HANDLE createThread(unsigned(__stdcall* threadProc)(void*), unsigned int* threadId, int priority, unsigned initFlags = 0);
	std::string getEnvVar(const char* var);
	void pinModule(HMODULE module);
	void pinModule(LPCSTR moduleName);
	void pinModule(LPCWSTR moduleName);

	extern HMODULE g_currentModule;
	extern HMODULE g_origDDrawModule;
	extern HMODULE g_origDciman32Module;
	extern Procs g_origProcs;
	extern Procs g_jmpTargetProcs;
	extern bool g_isHooked;
}

#undef  ADD_FARPROC_MEMBER

#define CALL_ORIG_PROC(procName) reinterpret_cast<decltype(procName)*>(Dll::g_origProcs.procName)
