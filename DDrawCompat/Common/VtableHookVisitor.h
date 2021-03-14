#pragma once

#include <string>
#include <typeinfo>
#include <type_traits>

#include <Common/Hook.h>
#include <Common/Log.h>

template <auto memberPtr, typename Interface, typename... Params>
auto __stdcall callOrigFunc(Interface This, Params... params)
{
	return (getOrigVtable(This).*memberPtr)(This, params...);
}

template <auto memberPtr, typename Vtable>
constexpr auto getCompatFunc(Vtable*)
{
	auto func = getCompatVtable<Vtable>().*memberPtr;
#ifdef DEBUGLOGS
	if (!func)
	{
		func = &callOrigFunc<memberPtr>;
	}
#endif
	return func;
}

template <auto memberPtr, typename Vtable>
constexpr auto getCompatFunc()
{
	return getCompatFunc<memberPtr>(static_cast<Vtable*>(nullptr));
}

template <typename Vtable>
constexpr auto getCompatVtable()
{
	Vtable vtable = {};
	setCompatVtable(vtable);
	return vtable;
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
		if constexpr (getCompatFunc<memberPtr, Vtable>())
		{
			if (m_vtable.*memberPtr)
			{
#ifdef DEBUGLOGS
				getFuncName<memberPtr>() = s_vtableTypeName + "::" + funcName;
				Compat::Log() << "Hooking function: " << getFuncName<memberPtr>()
					<< " (" << Compat::funcPtrToStr(m_vtable.*memberPtr) << ')';
#endif
				m_vtable.*memberPtr = &hookFunc<memberPtr>;
			}
		}
	}

private:
	template <auto memberPtr>
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

	template <auto memberPtr, typename Result, typename FirstParam, typename... Params>
	static Result STDMETHODCALLTYPE hookFunc(FirstParam firstParam, Params... params)
	{
#ifdef DEBUGLOGS
		const char* funcName = getFuncName<memberPtr>().c_str();
#endif
		LOG_FUNC(funcName, firstParam, params...);
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

	static std::string s_vtableTypeName;
};

#ifdef DEBUGLOGS
template <typename Vtable, typename Lock>
std::string VtableHookVisitor<Vtable, Lock>::s_vtableTypeName(getVtableTypeName());
#endif
