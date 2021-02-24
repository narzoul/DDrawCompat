#pragma once

#include <Common/VtableHookVisitor.h>
#include <Common/VtableVisitor.h>

#define SET_COMPAT_VTABLE(Vtable, CompatInterface) \
	namespace Compat \
	{ \
		inline void setCompatVtable(Vtable& vtable) \
		{ \
			CompatInterface::setCompatVtable(vtable); \
		} \
	}

template <typename Vtable>
class CompatVtableInstanceBase
{
public:
	static Vtable* s_origVtablePtr;
};

template <typename Vtable, typename Lock, int instanceId = -1>
class CompatVtableInstance : public CompatVtableInstanceBase<Vtable>
{
public:
	static void hookVtable(const Vtable& origVtable, Vtable compatVtable)
	{
		VtableHookVisitor<Vtable, Lock, instanceId> vtableHookVisitor(origVtable, s_origVtable, compatVtable);
		forEach<Vtable>(vtableHookVisitor);
		s_origVtablePtr = &s_origVtable;
	}

	static Vtable s_origVtable;
};

template <typename Vtable>
Vtable* CompatVtableInstanceBase<Vtable>::s_origVtablePtr = nullptr;

template <typename Vtable, typename Lock, int instanceId>
Vtable CompatVtableInstance<Vtable, Lock, instanceId>::s_origVtable = {};
