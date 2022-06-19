#pragma once

#include <Common/Time.h>

namespace Time
{
	long long g_qpcFrequency = 0;

	void init()
	{
		LARGE_INTEGER qpc;
		QueryPerformanceFrequency(&qpc);
		g_qpcFrequency = qpc.QuadPart;
	}

	void waitForNextTick()
	{
		thread_local HANDLE waitableTimer = CreateWaitableTimer(nullptr, FALSE, nullptr);

		LARGE_INTEGER due = {};
		due.QuadPart = -1;
		if (!waitableTimer ||
			!SetWaitableTimer(waitableTimer, &due, 0, nullptr, nullptr, FALSE) ||
			WAIT_OBJECT_0 != WaitForSingleObject(waitableTimer, INFINITE))
		{
			Sleep(1);
		}
	}
}
