#pragma once

#include "Common/LogWrapperVisitor.h"
#include "Common/VtableHookVisitor.h"
#include "Common/VtableUpdateVisitor.h"
#include "Common/VtableVisitor.h"

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

template <typename Vtable, int instanceId = -1>
class CompatVtableInstance : public CompatVtableInstanceBase<Vtable>
{
public:
	static void hookVtable(const Vtable& origVtable, Vtable compatVtable)
	{
#ifdef DEBUGLOGS
		LogWrapperVisitor<Vtable, instanceId> logWrapperVisitor(origVtable, compatVtable);
		forEach<Vtable>(logWrapperVisitor);
#endif

		VtableHookVisitor<Vtable, instanceId> vtableHookVisitor(origVtable, s_origVtable, compatVtable);
		forEach<Vtable>(vtableHookVisitor);

#ifdef DEBUGLOGS
		VtableUpdateVisitor<Vtable> vtableUpdateVisitor(
			origVtable, s_origVtable, LogWrapperVisitor<Vtable, instanceId>::s_compatVtable);
		forEach<Vtable>(vtableUpdateVisitor);
#endif

		s_origVtablePtr = &s_origVtable;
	}

	static Vtable s_origVtable;
};

template <typename Vtable>
Vtable* CompatVtableInstanceBase<Vtable>::s_origVtablePtr = nullptr;

template <typename Vtable, int instanceId>
Vtable CompatVtableInstance<Vtable, instanceId>::s_origVtable = {};
