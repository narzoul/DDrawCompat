#pragma once

#include <map>
#include <string>
#include <vector>

#include "Common/Hook.h"
#include "Common/Log.h"
#include "Common/VtableVisitor.h"
#include "DDrawProcs.h"

template <typename Interface>
using Vtable = typename std::remove_pointer<decltype(Interface::lpVtbl)>::type;

template <typename Interface>
class CompatVtableBase
{
public:
	typedef Interface Interface;

	static const Vtable<Interface>& getOrigVtable(Interface& intf)
	{
		return s_origVtable.AddRef ? s_origVtable : *intf.lpVtbl;
	}

	static Vtable<Interface> s_origVtable;
};

template <typename CompatInterface, typename Interface>
class CompatVtable : public CompatVtableBase<Interface>
{
public:
	static void hookVtable(const Vtable<Interface>* vtable)
	{
		static bool isInitialized = false;
		if (!isInitialized && vtable)
		{
			isInitialized = true;

			InitVisitor visitor(*vtable);
			forEach<Vtable<Interface>>(visitor);
		}
	}

private:
	class InitVisitor
	{
	public:
		InitVisitor(const Vtable<Interface>& origVtable) : m_origVtable(origVtable) {}

		template <typename MemberDataPtr, MemberDataPtr ptr>
		void visit()
		{
			s_origVtable.*ptr = m_origVtable.*ptr;

			if (!(s_compatVtable.*ptr))
			{
				s_threadSafeVtable.*ptr = s_origVtable.*ptr;
				s_compatVtable.*ptr = s_origVtable.*ptr;
			}
			else
			{
				s_threadSafeVtable.*ptr = getThreadSafeFuncPtr<MemberDataPtr, ptr>(s_compatVtable.*ptr);
				Compat::hookFunction(reinterpret_cast<void*&>(s_origVtable.*ptr), s_threadSafeVtable.*ptr);
			}
		}

		template <typename MemberDataPtr, MemberDataPtr ptr>
		void visitDebug(const std::string& vtableTypeName, const std::string& funcName)
		{
			Compat::Log() << "Hooking function: " << vtableTypeName << "::" << funcName;
			s_funcNames[getKey<MemberDataPtr, ptr>()] = vtableTypeName + "::" + funcName;

			s_origVtable.*ptr = m_origVtable.*ptr;

			s_threadSafeVtable.*ptr = getThreadSafeFuncPtr<MemberDataPtr, ptr>(s_compatVtable.*ptr);
			Compat::hookFunction(reinterpret_cast<void*&>(s_origVtable.*ptr), s_threadSafeVtable.*ptr);

			if (!(s_compatVtable.*ptr))
			{
				s_compatVtable.*ptr = s_origVtable.*ptr;
			}
		}

	private:
		template <typename Result, typename... Params>
		using FuncPtr = Result(STDMETHODCALLTYPE *)(Params...);

		template <typename MemberDataPtr, MemberDataPtr ptr>
		static std::vector<unsigned char> getKey()
		{
			MemberDataPtr mp = ptr;
			unsigned char* p = reinterpret_cast<unsigned char*>(&mp);
			return std::vector<unsigned char>(p, p + sizeof(mp));
		}

		template <typename MemberDataPtr, MemberDataPtr ptr, typename Result, typename... Params>
		static FuncPtr<Result, Params...> getThreadSafeFuncPtr(FuncPtr<Result, Params...>)
		{
			return &threadSafeFunc<MemberDataPtr, ptr, Result, Params...>;
		}

		template <typename MemberDataPtr, MemberDataPtr ptr, typename... Params>
		static FuncPtr<void, Params...> getThreadSafeFuncPtr(FuncPtr<void, Params...>)
		{
			return &threadSafeFunc<MemberDataPtr, ptr, Params...>;
		}

		template <typename MemberDataPtr, MemberDataPtr ptr, typename Result, typename... Params>
		static Result STDMETHODCALLTYPE threadSafeFunc(Params... params)
		{
			Compat::origProcs.AcquireDDThreadLock();
#ifdef _DEBUG
			Compat::LogEnter(s_funcNames[getKey<MemberDataPtr, ptr>()].c_str(), params...);
#endif

			Result result = (s_compatVtable.*ptr)(params...);

#ifdef _DEBUG
			Compat::LogLeave(s_funcNames[getKey<MemberDataPtr, ptr>()].c_str(), params...) << result;
#endif
			Compat::origProcs.ReleaseDDThreadLock();
			return result;
		}

		template <typename MemberDataPtr, MemberDataPtr ptr, typename... Params>
		static void STDMETHODCALLTYPE threadSafeFunc(Params... params)
		{
			Compat::origProcs.AcquireDDThreadLock();
#ifdef _DEBUG
			Compat::LogEnter(s_funcNames[getKey<MemberDataPtr, ptr>()].c_str(), params...);
#endif

			(s_compatVtable.*ptr)(params...);

#ifdef _DEBUG
			Compat::LogLeave(s_funcNames[getKey<MemberDataPtr, ptr>()].c_str(), params...);
#endif
			Compat::origProcs.ReleaseDDThreadLock();
		}

		const Vtable<Interface>& m_origVtable;
	};

	static Vtable<Interface> createCompatVtable()
	{
		Vtable<Interface> vtable = {};
		CompatInterface::setCompatVtable(vtable);
		return vtable;
	}

	static Vtable<Interface>& getCompatVtable()
	{
		static Vtable<Interface> vtable(createCompatVtable());
		return vtable;
	}

	static Vtable<Interface> s_compatVtable;
	static Vtable<Interface> s_threadSafeVtable;
	static std::map<std::vector<unsigned char>, std::string> s_funcNames;
};

template <typename Interface>
Vtable<Interface> CompatVtableBase<Interface>::s_origVtable = {};

template <typename CompatInterface, typename Interface>
Vtable<Interface> CompatVtable<CompatInterface, Interface>::s_compatVtable(getCompatVtable());

template <typename CompatInterface, typename Interface>
Vtable<Interface> CompatVtable<CompatInterface, Interface>::s_threadSafeVtable = {};

template <typename CompatInterface, typename Interface>
std::map<std::vector<unsigned char>, std::string> CompatVtable<CompatInterface, Interface>::s_funcNames;
