#pragma once

#include <Common/Time.h>

namespace Time
{
	long long g_qpcFrequency = 0;
	HANDLE g_waitableTimer = nullptr;

	void init()
	{
		LARGE_INTEGER qpc;
		QueryPerformanceFrequency(&qpc);
		g_qpcFrequency = qpc.QuadPart;

		g_waitableTimer = CreateWaitableTimer(nullptr, FALSE, nullptr);
	}

	void waitForNextTick()
	{
		LARGE_INTEGER due = {};
		due.QuadPart = -1;
		if (!g_waitableTimer ||
			!SetWaitableTimer(g_waitableTimer, &due, 0, nullptr, nullptr, FALSE) ||
			WAIT_OBJECT_0 != WaitForSingleObject(g_waitableTimer, INFINITE))
		{
			Sleep(1);
		}
	}
}
