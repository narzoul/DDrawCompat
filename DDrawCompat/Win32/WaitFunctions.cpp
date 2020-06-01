#include <Windows.h>
#include <timeapi.h>

#include <Common/Hook.h>
#include <Common/Time.h>
#include <Config/Config.h>
#include <Win32/WaitFunctions.h>

namespace
{
	void mitigateBusyWaiting()
	{
		thread_local ULONG64 ctLastThreadSwitch = Time::queryThreadCycleTime();
		ULONG64 ctNow = Time::queryThreadCycleTime();
		if (ctNow - ctLastThreadSwitch >= Config::threadSwitchCycleTime)
		{
			Sleep(0);
			ctLastThreadSwitch = ctNow;
		}
	}

	template <typename FuncPtr, FuncPtr func, typename Result, typename... Params>
	Result WINAPI mitigatedBusyWaitingFunc(Params... params)
	{
		mitigateBusyWaiting();
		return Compat::getOrigFuncPtr<FuncPtr, func>()(params...);
	}
}

#define MITIGATE_BUSY_WAITING(module, func) \
		Compat::hookFunction<decltype(&func), &func>(#module, #func, &mitigatedBusyWaitingFunc<decltype(&func), func>)

namespace Win32
{
	namespace WaitFunctions
	{
		void installHooks()
		{
			MITIGATE_BUSY_WAITING(user32, GetMessageA);
			MITIGATE_BUSY_WAITING(user32, GetMessageW);
			MITIGATE_BUSY_WAITING(kernel32, GetTickCount);
			MITIGATE_BUSY_WAITING(user32, MsgWaitForMultipleObjects);
			MITIGATE_BUSY_WAITING(user32, MsgWaitForMultipleObjectsEx);
			MITIGATE_BUSY_WAITING(user32, PeekMessageA);
			MITIGATE_BUSY_WAITING(user32, PeekMessageW);
			MITIGATE_BUSY_WAITING(kernel32, SignalObjectAndWait);
			MITIGATE_BUSY_WAITING(winmm, timeGetTime);
			MITIGATE_BUSY_WAITING(kernel32, WaitForSingleObjectEx);
			MITIGATE_BUSY_WAITING(kernel32, WaitForMultipleObjectsEx);
		}
	}
}
