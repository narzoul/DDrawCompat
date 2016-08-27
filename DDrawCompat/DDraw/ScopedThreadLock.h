#pragma once

#include "DDrawProcs.h"

namespace DDraw
{
	class ScopedThreadLock
	{
	public:
		ScopedThreadLock()
		{
			Compat::origProcs.AcquireDDThreadLock();
		}

		~ScopedThreadLock()
		{
			Compat::origProcs.ReleaseDDThreadLock();
		}
	};
}
