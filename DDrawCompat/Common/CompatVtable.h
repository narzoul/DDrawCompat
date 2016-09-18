#pragma once

#include <map>
#include <string>
#include <vector>

#include "Common/Hook.h"
#include "Common/Log.h"
#include "Common/VtableVisitor.h"
#include "DDraw/ScopedThreadLock.h"

#define SET_COMPAT_VTABLE(Vtable, CompatInterface) \
	namespace Compat \
	{ \
		inline void setCompatVtable(Vtable& vtable) \
		{ \
			CompatInterface::setCompatVtable(vtable); \
		} \
	}

template <typename Interface>
using Vtable = typename std::remove_pointer<decltype(Interface::lpVtbl)>::type;

template <typename Vtable>
class CompatVtable
{
public:
	static const Vtable& getOrigVtable(const Vtable& vtable)
	{
		return s_origVtable.AddRef ? s_origVtable : vtable;
	}

	static void hookVtable(const Vtable* vtable)
	{
		if (vtable && !s_origVtablePtr)
		{
			s_origVtablePtr = vtable;

			InitVisitor visitor(*vtable);
			forEach<Vtable>(visitor);
		}
	}

	static Vtable s_origVtable;
	static const Vtable* s_origVtablePtr;

private:
	class InitVisitor
	{
	public:
		InitVisitor(const Vtable& origVtable) : m_origVtable(origVtable) {}

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
			DDraw::ScopedThreadLock lock;
#ifdef _DEBUG
			Compat::LogEnter(s_funcNames[getKey<MemberDataPtr, ptr>()].c_str(), params...);
#endif

			Result result = (s_compatVtable.*ptr)(params...);

#ifdef _DEBUG
			Compat::LogLeave(s_funcNames[getKey<MemberDataPtr, ptr>()].c_str(), params...) << result;
#endif
			return result;
		}

		template <typename MemberDataPtr, MemberDataPtr ptr, typename... Params>
		static void STDMETHODCALLTYPE threadSafeFunc(Params... params)
		{
			DDraw::ScopedThreadLock lock;
#ifdef _DEBUG
			Compat::LogEnter(s_funcNames[getKey<MemberDataPtr, ptr>()].c_str(), params...);
#endif

			(s_compatVtable.*ptr)(params...);

#ifdef _DEBUG
			Compat::LogLeave(s_funcNames[getKey<MemberDataPtr, ptr>()].c_str(), params...);
#endif
		}

		const Vtable& m_origVtable;
	};

	static Vtable createCompatVtable()
	{
		Vtable vtable = {};
		Compat::setCompatVtable(vtable);
		return vtable;
	}

	static Vtable& getCompatVtable()
	{
		static Vtable vtable(createCompatVtable());
		return vtable;
	}

	static Vtable s_compatVtable;
	static Vtable s_threadSafeVtable;
	static std::map<std::vector<unsigned char>, std::string> s_funcNames;
};

template <typename Vtable>
Vtable CompatVtable<Vtable>::s_origVtable = {};

template <typename Vtable>
const Vtable* CompatVtable<Vtable>::s_origVtablePtr = nullptr;

template <typename Vtable>
Vtable CompatVtable<Vtable>::s_compatVtable(getCompatVtable());

template <typename Vtable>
Vtable CompatVtable<Vtable>::s_threadSafeVtable = {};

template <typename Vtable>
std::map<std::vector<unsigned char>, std::string> CompatVtable<Vtable>::s_funcNames;
