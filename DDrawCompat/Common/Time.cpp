#pragma once

#include <Windows.h>

#include <Common/Log.h>
#include <Common/ScopedSrwLock.h>
#include <Common/Time.h>
#include <Dll/Dll.h>

namespace
{
	CONDITION_VARIABLE g_tickCounterCv = CONDITION_VARIABLE_INIT;
	Compat::SrwLock g_tickCounterSrwLock;
	DWORD g_tickCounter = 0;
	HANDLE g_timer = nullptr;

	void CALLBACK onTimer(UINT /*uTimerID*/, UINT /*uMsg*/, DWORD_PTR /*dwUser*/, DWORD_PTR /*dw1*/, DWORD_PTR /*dw2*/)
	{
		{
			Compat::ScopedSrwLockExclusive lock(g_tickCounterSrwLock);
			++g_tickCounter;
		}

		WakeAllConditionVariable(&g_tickCounterCv);
	}

	unsigned WINAPI tickThreadProc(LPVOID /*lpParameter*/)
	{
		g_timer = CreateWaitableTimer(nullptr, FALSE, nullptr);
		if (!g_timer)
		{
			LOG_INFO << "ERROR: Failed to create a tick timer: " << GetLastError();
			return 0;
		}

		LARGE_INTEGER due = {};
		due.QuadPart = -1;
		while (SetWaitableTimer(g_timer, &due, 0, nullptr, nullptr, FALSE) &&
			WAIT_OBJECT_0 == WaitForSingleObject(g_timer, 10))
		{
			{
				Compat::ScopedSrwLockExclusive lock(g_tickCounterSrwLock);
				++g_tickCounter;
			}
			WakeAllConditionVariable(&g_tickCounterCv);
		}

		LOG_INFO << "ERROR: Tick timer failed";
		CloseHandle(g_timer);
		g_timer = nullptr;
		WakeAllConditionVariable(&g_tickCounterCv);
		return 0;
	}
}

namespace Time
{
	long long g_qpcFrequency = 0;

	void init()
	{
		LARGE_INTEGER qpc = {};
		QueryPerformanceFrequency(&qpc);
		g_qpcFrequency = qpc.QuadPart;

		Dll::createThread(&tickThreadProc, nullptr, THREAD_PRIORITY_TIME_CRITICAL);
	}

	void waitForNextTick()
	{
		if (!g_timer)
		{
			Sleep(1);
			return;
		}

		Compat::ScopedSrwLockShared lock(g_tickCounterSrwLock);
		const DWORD counter = g_tickCounter;
		while (counter == g_tickCounter)
		{
			if (!SleepConditionVariableSRW(&g_tickCounterCv, &g_tickCounterSrwLock, 1, CONDITION_VARIABLE_LOCKMODE_SHARED))
			{
				if (ERROR_TIMEOUT != GetLastError())
				{
					Sleep(1);
				}
				return;
			}
		}
	}
}
