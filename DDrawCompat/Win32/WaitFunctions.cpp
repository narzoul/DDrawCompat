#include <Windows.h>

#include <Config/Config.h>
#include <Common/Hook.h>
#include <Win32/WaitFunctions.h>

namespace
{
	DWORD mitigateBusyWaiting(DWORD dwMilliseconds, DWORD waitResult)
	{
		if (0 == dwMilliseconds && WAIT_TIMEOUT == waitResult)
		{
			SwitchToThread();
		}
		return waitResult;
	}

	DWORD WINAPI msgWaitForMultipleObjects(
		DWORD nCount, const HANDLE* pHandles, BOOL fWaitAll, DWORD dwMilliseconds, DWORD dwWakeMask)
	{
		return mitigateBusyWaiting(dwMilliseconds, CALL_ORIG_FUNC(MsgWaitForMultipleObjects)(
			nCount, pHandles, fWaitAll, dwMilliseconds, dwWakeMask));
	}

	DWORD WINAPI msgWaitForMultipleObjectsEx(
		DWORD nCount, const HANDLE* pHandles, DWORD dwMilliseconds, DWORD dwWakeMask, DWORD dwFlags)
	{
		return mitigateBusyWaiting(dwMilliseconds, CALL_ORIG_FUNC(MsgWaitForMultipleObjectsEx)(
			nCount, pHandles, dwMilliseconds, dwWakeMask, dwFlags));
	}

	DWORD WINAPI signalObjectAndWait(
		HANDLE hObjectToSignal, HANDLE hObjectToWaitOn, DWORD dwMilliseconds, BOOL bAlertable)
	{
		return mitigateBusyWaiting(dwMilliseconds, CALL_ORIG_FUNC(SignalObjectAndWait)(
			hObjectToSignal, hObjectToWaitOn, dwMilliseconds, bAlertable));
	}

	DWORD WINAPI waitForSingleObjectEx(HANDLE hHandle, DWORD dwMilliseconds, BOOL bAlertable)
	{
		return mitigateBusyWaiting(dwMilliseconds, CALL_ORIG_FUNC(WaitForSingleObjectEx)(
			hHandle, dwMilliseconds, bAlertable));
	}

	DWORD WINAPI waitForMultipleObjectsEx(
		DWORD nCount, const HANDLE* lpHandles, BOOL bWaitAll, DWORD dwMilliseconds, BOOL bAlertable)
	{
		return mitigateBusyWaiting(dwMilliseconds, CALL_ORIG_FUNC(WaitForMultipleObjectsEx)(
			nCount, lpHandles, bWaitAll, dwMilliseconds, bAlertable));
	}
}

namespace Win32
{
	namespace WaitFunctions
	{
		void installHooks()
		{
			HOOK_FUNCTION(user32, MsgWaitForMultipleObjects, msgWaitForMultipleObjects);
			HOOK_FUNCTION(user32, MsgWaitForMultipleObjectsEx, msgWaitForMultipleObjectsEx);
			HOOK_FUNCTION(kernel32, SignalObjectAndWait, signalObjectAndWait);
			HOOK_FUNCTION(kernel32, WaitForSingleObjectEx, waitForSingleObjectEx);
			HOOK_FUNCTION(kernel32, WaitForMultipleObjectsEx, waitForMultipleObjectsEx);
		}
	}
}
