#pragma once

#include <ostream>

#include "Common/CompatVtable.h"

template <typename Intf>
class CompatRef
{
public:
	CompatRef(Intf& intf) : m_intf(intf)
	{
	}

	const Vtable<Intf>* operator->() const
	{
		return &CompatVtable<Vtable<Intf>>::getOrigVtable(*m_intf.lpVtbl);
	}

	Intf* operator&() const
	{
		return &m_intf;
	}

	Intf& get() const
	{
		return m_intf;
	}

private:
	Intf& m_intf;
};

template <typename T>
std::ostream& operator<<(std::ostream& os, const CompatRef<T>& ref)
{
	return os << &ref;
}
