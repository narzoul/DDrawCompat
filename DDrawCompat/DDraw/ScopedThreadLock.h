#pragma once

#include <Dll/Dll.h>

namespace DDraw
{
	class ScopedThreadLock
	{
	public:
		ScopedThreadLock()
		{
			Dll::g_origProcs.AcquireDDThreadLock();
		}

		~ScopedThreadLock()
		{
			Dll::g_origProcs.ReleaseDDThreadLock();
		}
	};

	class ScopedThreadUnlock
	{
	public:
		ScopedThreadUnlock()
		{
			Dll::g_origProcs.ReleaseDDThreadLock();
		}

		~ScopedThreadUnlock()
		{
			Dll::g_origProcs.AcquireDDThreadLock();
		}
	};
}
