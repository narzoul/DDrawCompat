#pragma once

#include <Common/ScopedCriticalSection.h>

namespace D3dDdi
{
	class ScopedCriticalSection : public Compat::ScopedCriticalSection
	{
	public:
		ScopedCriticalSection();
		~ScopedCriticalSection();

	private:
		static Compat::CriticalSection s_cs;
	};
}
