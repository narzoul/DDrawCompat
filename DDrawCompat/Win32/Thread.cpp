#include <Windows.h>

#include <Common/Hook.h>
#include <Common/Log.h>
#include <Config/Config.h>
#include <Win32/Thread.h>

namespace
{
	BOOL WINAPI setProcessAffinityMask(HANDLE hProcess, DWORD_PTR dwProcessAffinityMask)
	{
		LOG_FUNC("SetProcessAffinityMask", hProcess, Compat::hex(dwProcessAffinityMask));
		if (0 != Config::cpuAffinity.get())
		{
			return LOG_RESULT(TRUE);
		}
		return LOG_RESULT(CALL_ORIG_FUNC(SetProcessAffinityMask)(hProcess, dwProcessAffinityMask));
	}

	BOOL WINAPI setProcessPriorityBoost(HANDLE hProcess, BOOL bDisablePriorityBoost)
	{
		LOG_FUNC("SetProcessPriorityBoost", hProcess, bDisablePriorityBoost);
		if (Config::Settings::ThreadPriorityBoost::APP != Config::threadPriorityBoost.get())
		{
			return LOG_RESULT(TRUE);
		}
		return LOG_RESULT(CALL_ORIG_FUNC(SetProcessPriorityBoost)(hProcess, bDisablePriorityBoost));
	}

	BOOL WINAPI setThreadPriorityBoost(HANDLE hThread, BOOL bDisablePriorityBoost)
	{
		LOG_FUNC("SetThreadPriorityBoost", hThread, bDisablePriorityBoost);
		if (Config::Settings::ThreadPriorityBoost::APP != Config::threadPriorityBoost.get())
		{
			return LOG_RESULT(TRUE);
		}
		return LOG_RESULT(CALL_ORIG_FUNC(SetThreadPriorityBoost)(hThread, bDisablePriorityBoost));
	}
}

namespace Win32
{
	namespace Thread
	{
		void applyConfig()
		{
			auto cpuAffinity = Config::cpuAffinity.get();
			if (0 != cpuAffinity)
			{
				SYSTEM_INFO si = {};
				GetSystemInfo(&si);
				const unsigned cpuCount = min(si.dwNumberOfProcessors, 32);
				cpuAffinity &= UINT_MAX >> (32 - cpuCount);

				if (0 == cpuAffinity || !CALL_ORIG_FUNC(SetProcessAffinityMask)(GetCurrentProcess(), cpuAffinity))
				{
					Compat::Log() << (0 == cpuAffinity ? "Invalid" : "Failed to set") << " CPU affinity, falling back to default";
					Config::cpuAffinity.reset();
					CALL_ORIG_FUNC(SetProcessAffinityMask)(GetCurrentProcess(), Config::cpuAffinity.get());
				}
			}

			switch (Config::threadPriorityBoost.get())
			{
			case Config::Settings::ThreadPriorityBoost::OFF:
				CALL_ORIG_FUNC(SetProcessPriorityBoost)(GetCurrentProcess(), TRUE);
				break;

			case Config::Settings::ThreadPriorityBoost::ON:
				CALL_ORIG_FUNC(SetProcessPriorityBoost)(GetCurrentProcess(), FALSE);
				break;

			case Config::Settings::ThreadPriorityBoost::MAIN:
				CALL_ORIG_FUNC(SetProcessPriorityBoost)(GetCurrentProcess(), TRUE);
				CALL_ORIG_FUNC(SetThreadPriorityBoost)(GetCurrentThread(), FALSE);
				break;
			}
		}

		void installHooks()
		{
			HOOK_FUNCTION(kernel32, SetProcessAffinityMask, setProcessAffinityMask);
			HOOK_FUNCTION(kernel32, SetProcessPriorityBoost, setProcessPriorityBoost);
			HOOK_FUNCTION(kernel32, SetThreadPriorityBoost, setThreadPriorityBoost);
		}
	}
}
