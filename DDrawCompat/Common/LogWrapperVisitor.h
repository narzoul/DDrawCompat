#pragma once

#include <Common/FuncNameVisitor.h>
#include <Common/Log.h>

template <typename Vtable, int instanceId = 0>
class LogWrapperVisitor
{
public:
	LogWrapperVisitor(const Vtable& origVtable, Vtable& compatVtable)
		: m_origVtable(origVtable)
		, m_compatVtable(compatVtable)
	{
	}

	template <typename MemberDataPtr, MemberDataPtr ptr>
	void visit(const char* funcName)
	{
		FuncNameVisitor<Vtable>::visit<MemberDataPtr, ptr>(funcName);

		if (!(m_compatVtable.*ptr))
		{
			m_compatVtable.*ptr = m_origVtable.*ptr;
		}

		s_compatVtable.*ptr = m_compatVtable.*ptr;
		m_compatVtable.*ptr = getLoggedFuncPtr<MemberDataPtr, ptr>(m_compatVtable.*ptr);
	}

	static Vtable s_compatVtable;

private:
	template <typename Result, typename... Params>
	using FuncPtr = Result(STDMETHODCALLTYPE *)(Params...);

	template <typename MemberDataPtr, MemberDataPtr ptr, typename Result, typename... Params>
	static FuncPtr<Result, Params...> getLoggedFuncPtr(FuncPtr<Result, Params...>)
	{
		return &loggedFunc<MemberDataPtr, ptr, Result, Params...>;
	}

	template <typename MemberDataPtr, MemberDataPtr ptr, typename... Params>
	static FuncPtr<void, Params...> getLoggedFuncPtr(FuncPtr<void, Params...>)
	{
		return &loggedFunc<MemberDataPtr, ptr, Params...>;
	}

	template <typename MemberDataPtr, MemberDataPtr ptr, typename Result, typename... Params>
	static Result STDMETHODCALLTYPE loggedFunc(Params... params)
	{
		const char* funcName = FuncNameVisitor<Vtable>::getFuncName<MemberDataPtr, ptr>();
		LOG_FUNC(funcName, params...);
		return LOG_RESULT((s_compatVtable.*ptr)(params...));
	}

	template <typename MemberDataPtr, MemberDataPtr ptr, typename... Params>
	static void STDMETHODCALLTYPE loggedFunc(Params... params)
	{
		const char* funcName = FuncNameVisitor<Vtable>::getFuncName<MemberDataPtr, ptr>();
		LOG_FUNC(funcName, params...);
		(s_compatVtable.*ptr)(params...);
	}

	const Vtable& m_origVtable;
	Vtable& m_compatVtable;
};

template <typename Vtable, int instanceId>
Vtable LogWrapperVisitor<Vtable, instanceId>::s_compatVtable;
