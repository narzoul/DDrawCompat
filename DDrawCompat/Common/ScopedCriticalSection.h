#pragma once

#define WIN32_LEAN_AND_MEAN

#include <Windows.h>

namespace Compat
{
	class CriticalSection : public CRITICAL_SECTION
	{
	public:
		CriticalSection()
		{
			InitializeCriticalSection(this);
		}

		~CriticalSection()
		{
			DeleteCriticalSection(this);
		}
	};

	class ScopedCriticalSection
	{
	public:
		ScopedCriticalSection(CRITICAL_SECTION& cs)
			: m_cs(cs)
		{
			EnterCriticalSection(&m_cs);
		}

		~ScopedCriticalSection()
		{
			LeaveCriticalSection(&m_cs);
		}

	private:
		CRITICAL_SECTION& m_cs;
	};
};
