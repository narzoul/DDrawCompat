#pragma once

#include <string>
#include <typeinfo>
#include <type_traits>

#include <Common/Hook.h>
#include <Common/Log.h>

template <typename Vtable>
class VtableHookVisitorBase
{
protected:
	template <typename MemberDataPtr, MemberDataPtr ptr>
	static std::string& getFuncName()
	{
		static std::string funcName;
		return funcName;
	}

	static std::string getVtableTypeName()
	{
		std::string name = typeid(Vtable).name();
		if (0 == name.find("struct "))
		{
			name = name.substr(name.find(" ") + 1);
		}
		return name;
	}

	static std::string s_vtableTypeName;
};

template <typename Vtable, typename Lock, int instanceId>
class VtableHookVisitor : public VtableHookVisitorBase<Vtable>
{
public:
	VtableHookVisitor(const Vtable& hookedVtable, Vtable& origVtable, const Vtable& compatVtable)
		: m_hookedVtable(const_cast<Vtable&>(hookedVtable))
		, m_origVtable(origVtable)
	{
		s_compatVtable = compatVtable;
	}

	template <typename MemberDataPtr, MemberDataPtr ptr>
	void visit([[maybe_unused]] const char* funcName)
	{
#ifdef DEBUGLOGS
		getFuncName<MemberDataPtr, ptr>() = s_vtableTypeName + "::" + funcName;
		if (!(s_compatVtable.*ptr))
		{
			s_compatVtable.*ptr = m_hookedVtable.*ptr;
		}
#endif

		m_origVtable.*ptr = m_hookedVtable.*ptr;
		if (m_hookedVtable.*ptr && s_compatVtable.*ptr)
		{
			LOG_DEBUG << "Hooking function: " << getFuncName<MemberDataPtr, ptr>()
				<< " (" << Compat::funcPtrToStr(m_hookedVtable.*ptr) << ')';
			DWORD oldProtect = 0;
			VirtualProtect(&(m_hookedVtable.*ptr), sizeof(m_hookedVtable.*ptr), PAGE_READWRITE, &oldProtect);
			m_hookedVtable.*ptr = &hookFunc<MemberDataPtr, ptr>;
			VirtualProtect(&(m_hookedVtable.*ptr), sizeof(m_hookedVtable.*ptr), oldProtect, &oldProtect);
		}
	}

private:
	template <typename MemberDataPtr, MemberDataPtr ptr, typename Result, typename... Params>
	static Result STDMETHODCALLTYPE hookFunc(Params... params)
	{
#ifdef DEBUGLOGS
		const char* funcName = getFuncName<MemberDataPtr, ptr>().c_str();
#endif
		LOG_FUNC(funcName, params...);
		Lock lock;
		if constexpr (-1 != instanceId)
		{
			CompatVtableInstanceBase<Vtable>::s_origVtablePtr = &CompatVtableInstance<Vtable, Lock, instanceId>::s_origVtable;
		}
		if constexpr (std::is_same_v<Result, void>)
		{
			(s_compatVtable.*ptr)(params...);
		}
		else
		{
			return LOG_RESULT((s_compatVtable.*ptr)(params...));
		}
	}

	Vtable& m_hookedVtable;
	Vtable& m_origVtable;

	static Vtable s_compatVtable;
};

#ifdef DEBUGLOGS
template <typename Vtable>
std::string VtableHookVisitorBase<Vtable>::s_vtableTypeName(getVtableTypeName());
#endif

template <typename Vtable, typename Lock, int instanceId>
Vtable VtableHookVisitor<Vtable, Lock, instanceId>::s_compatVtable = {};
