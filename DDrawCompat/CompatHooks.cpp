#define WIN32_LEAN_AND_MEAN

#include <Windows.h>

#include "CompatHooks.h"
#include "Hook.h"

namespace
{
	HHOOK WINAPI setWindowsHookExA(int idHook, HOOKPROC lpfn, HINSTANCE hMod, DWORD dwThreadId)
	{
		if (WH_KEYBOARD_LL == idHook && hMod && GetModuleHandle("AcGenral") == hMod)
		{
			return nullptr;
		}
		return CALL_ORIG_FUNC(SetWindowsHookExA)(idHook, lpfn, hMod, dwThreadId);
	}
}

namespace CompatHooks
{
	void installHooks()
	{
		HOOK_FUNCTION(user32, SetWindowsHookExA, setWindowsHookExA);
	}
}
