#include <Windows.h>
#include <timeapi.h>

#include <Common/Hook.h>
#include <Win32/TimeFunctions.h>

namespace
{
	typedef DWORD(WINAPI* TimeFunc)();

	template <TimeFunc timeFunc>
	DWORD WINAPI getTime()
	{
		thread_local DWORD prevTime = 0;
		auto origTimeFunc = Compat::getOrigFuncPtr<TimeFunc, timeFunc>();
		DWORD time = origTimeFunc();
		if (prevTime == time)
		{
			SwitchToThread();
			time = origTimeFunc();
		}
		prevTime = time;
		return time;
	}
}

namespace Win32
{
	namespace TimeFunctions
	{
		void installHooks()
		{
			HOOK_FUNCTION(kernel32, GetTickCount, getTime<&GetTickCount>);
			HOOK_FUNCTION(winmm, timeGetTime, getTime<&timeGetTime>);
		}
	}
}
