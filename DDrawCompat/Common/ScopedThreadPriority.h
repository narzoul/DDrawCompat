#pragma once

#include <Windows.h>

namespace Compat
{
	class ScopedThreadPriority
	{
	public:
		ScopedThreadPriority(int priority)
			: m_prevPriority(GetThreadPriority(GetCurrentThread()))
		{
			SetThreadPriority(GetCurrentThread(), priority);
		}

		~ScopedThreadPriority()
		{
			SetThreadPriority(GetCurrentThread(), m_prevPriority);
		}

	private:
		int m_prevPriority;
	};
}
