#pragma once

#include <Common/ScopedCriticalSection.h>

namespace D3dDdi
{
	class ScopedCriticalSection : public Compat::ScopedCriticalSection
	{
	public:
		ScopedCriticalSection() : Compat::ScopedCriticalSection(s_cs) {}

	private:
		static Compat::CriticalSection s_cs;
	};
}
