#pragma once

#include <Common/CompatVtableInstance.h>
#include <DDraw/ScopedThreadLock.h>

template <typename Interface>
using Vtable = typename std::remove_pointer<decltype(Interface::lpVtbl)>::type;

template <typename Vtable>
class CompatVtable : public CompatVtableInstance<Vtable, DDraw::ScopedThreadLock>
{
public:
	static const Vtable& getOrigVtable(const Vtable& vtable)
	{
		return s_origVtable.AddRef ? s_origVtable : vtable;
	}

	static void hookVtable(const Vtable* vtable)
	{
		if (!s_origVtablePtr && vtable)
		{
			s_origVtablePtr = vtable;
			Vtable compatVtable = {};
			Compat::setCompatVtable(compatVtable);
			CompatVtableInstance::hookVtable(*vtable, compatVtable);
		}
	}

	static const Vtable* s_origVtablePtr;
};

template <typename Vtable>
const Vtable* CompatVtable<Vtable>::s_origVtablePtr = nullptr;
