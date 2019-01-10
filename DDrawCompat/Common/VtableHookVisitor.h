#pragma once

#include "Common/FuncNameVisitor.h"
#include "Common/Hook.h"
#include "D3dDdi/ScopedCriticalSection.h"
#include "DDraw/ScopedThreadLock.h"

struct _D3DDDI_ADAPTERCALLBACKS;
struct _D3DDDI_DEVICEALLBACKS;

template <typename Vtable>
class ScopedVtableFuncLock : public DDraw::ScopedThreadLock {};

template <>
class ScopedVtableFuncLock<_D3DDDI_ADAPTERCALLBACKS> : public D3dDdi::ScopedCriticalSection {};

template <>
class ScopedVtableFuncLock<_D3DDDI_DEVICEALLBACKS> : public D3dDdi::ScopedCriticalSection {};

template <typename Vtable, int instanceId = 0>
class VtableHookVisitor
{
public:
	VtableHookVisitor(const Vtable& srcVtable, Vtable& origVtable, const Vtable& compatVtable)
		: m_srcVtable(srcVtable)
		, m_origVtable(origVtable)
	{
		s_compatVtable = compatVtable;
	}

	template <typename MemberDataPtr, MemberDataPtr ptr>
	void visit(const char* /*funcName*/)
	{
		m_origVtable.*ptr = m_srcVtable.*ptr;
		if (m_origVtable.*ptr && s_compatVtable.*ptr)
		{
#ifdef DEBUGLOGS
			Compat::Log() << "Hooking function: " << FuncNameVisitor<Vtable>::getFuncName<MemberDataPtr, ptr>();
#endif
			Compat::hookFunction(reinterpret_cast<void*&>(m_origVtable.*ptr), 
				getThreadSafeFuncPtr<MemberDataPtr, ptr>(s_compatVtable.*ptr));
		}
	}

private:
	template <typename Result, typename... Params>
	using FuncPtr = Result(STDMETHODCALLTYPE *)(Params...);

	template <typename MemberDataPtr, MemberDataPtr ptr, typename Result, typename... Params>
	static FuncPtr<Result, Params...> getThreadSafeFuncPtr(FuncPtr<Result, Params...>)
	{
		return &threadSafeFunc<MemberDataPtr, ptr, Result, Params...>;
	}

	template <typename MemberDataPtr, MemberDataPtr ptr, typename Result, typename... Params>
	static Result STDMETHODCALLTYPE threadSafeFunc(Params... params)
	{
		ScopedVtableFuncLock<Vtable> lock;
		return (s_compatVtable.*ptr)(params...);
	}

	const Vtable& m_srcVtable;
	Vtable& m_origVtable;

	static Vtable s_compatVtable;
};

template <typename Vtable, int instanceId>
Vtable VtableHookVisitor<Vtable, instanceId>::s_compatVtable = {};
