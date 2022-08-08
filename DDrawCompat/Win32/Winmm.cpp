#include <Windows.h>
#include <timeapi.h>

#include <Common/Hook.h>
#include <Common/Log.h>
#include <Win32/Winmm.h>

namespace
{
	MMRESULT WINAPI TimeBeginPeriod(UINT uPeriod)
	{
		LOG_FUNC("timeBeginPeriod", uPeriod);
		return LOG_RESULT(TIMERR_NOERROR);
	}

	MMRESULT WINAPI TimeEndPeriod(UINT uPeriod)
	{
		LOG_FUNC("timeEndPeriod", uPeriod);
		return LOG_RESULT(TIMERR_NOERROR);
	}
}

namespace Win32
{
	namespace Winmm
	{
		void installHooks()
		{
			if (Compat::getProcAddress(GetModuleHandle("kernel32"), "timeBeginPeriod"))
			{
				HOOK_FUNCTION(kernel32, timeBeginPeriod, TimeBeginPeriod);
				HOOK_FUNCTION(kernel32, timeEndPeriod, TimeEndPeriod);
			}
			else
			{
				HOOK_FUNCTION(winmm, timeBeginPeriod, TimeBeginPeriod);
				HOOK_FUNCTION(winmm, timeEndPeriod, TimeEndPeriod);
			}
		}
	}
}
