#pragma once

#include <string>
#include <typeinfo>
#include <type_traits>

#include <Common/Hook.h>
#include <Common/Log.h>

template <typename Interface>
using Vtable = typename std::remove_pointer<decltype(Interface::lpVtbl)>::type;

template <typename Vtable>
class CompatVtable;

template <typename Vtable, typename Interface>
const Vtable& getOrigVtable(Interface* /*This*/)
{
	return CompatVtable<Vtable>::s_origVtable;
}

template <>
inline const IUnknownVtbl& getOrigVtable(IUnknown* This)
{
	return *This->lpVtbl;
}

template <typename Interface>
const Vtable<Interface>& getOrigVtable(Interface* This)
{
	return getOrigVtable<Vtable<Interface>, Interface>(This);
}

template <typename Vtable>
const Vtable& getOrigVtable(HANDLE);

namespace
{
	template <typename Vtable>
	constexpr void setCompatVtable(Vtable&);
}

template <typename Vtable, auto memberPtr, typename Interface, typename... Params>
auto __stdcall callOrigFunc(Interface This, Params... params)
{
	return (getOrigVtable<Vtable>(This).*memberPtr)(This, params...);
}

template <typename Vtable>
constexpr auto getCompatVtable()
{
	Vtable vtable = {};
	setCompatVtable(vtable);
	return vtable;
}

template <auto memberPtr, typename Vtable>
constexpr auto getCompatFunc(Vtable*)
{
	auto func = getCompatVtable<Vtable>().*memberPtr;
	if (!func)
	{
		func = &callOrigFunc<Vtable, memberPtr>;
	}
	return func;
}

template <auto memberPtr, typename Vtable>
constexpr auto getCompatFunc()
{
	return getCompatFunc<memberPtr>(static_cast<Vtable*>(nullptr));
}

template <typename Vtable, typename Lock>
class VtableHookVisitor
{
public:
	VtableHookVisitor(const Vtable& vtable)
		: m_vtable(const_cast<Vtable&>(vtable))
	{
	}

	template <auto memberPtr>
	void visit([[maybe_unused]] const char* funcName)
	{
		if constexpr (!(getCompatVtable<Vtable>().*memberPtr))
		{
			if (Compat::Log::getLogLevel() < Config::Settings::LogLevel::DEBUG)
			{
				return;
			}
		}

		if (m_vtable.*memberPtr)
		{
			s_funcName<memberPtr> = s_vtableTypeName + "::" + funcName;
			LOG_DEBUG << "Hooking function: " << s_funcName<memberPtr>
				<< " (" << Compat::funcPtrToStr(m_vtable.*memberPtr) << ')';
			m_vtable.*memberPtr = &hookFunc<memberPtr>;
		}
	}

private:
	template <auto memberPtr, typename Result, typename FirstParam, typename... Params>
	static Result STDMETHODCALLTYPE hookFunc(FirstParam firstParam, Params... params)
	{
		LOG_FUNC(s_funcName<memberPtr>.c_str(), firstParam, params...);
		[[maybe_unused]] Lock lock;
		constexpr auto compatFunc = getCompatFunc<memberPtr, Vtable>();
		if constexpr (std::is_void_v<Result>)
		{
			compatFunc(firstParam, params...);
		}
		else
		{
			return LOG_RESULT(compatFunc(firstParam, params...));
		}
	}

	Vtable& m_vtable;

	template <auto memberPtr>
	static std::string s_funcName;

	static std::string s_vtableTypeName;
};

template <typename Vtable, typename Lock>
template <auto memberPtr>
std::string VtableHookVisitor<Vtable, Lock>::s_funcName;

template <typename Vtable, typename Lock>
std::string VtableHookVisitor<Vtable, Lock>::s_vtableTypeName(Compat::getTypeName<Vtable>());
