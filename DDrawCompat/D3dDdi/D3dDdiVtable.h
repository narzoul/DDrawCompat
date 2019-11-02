#pragma once

#include <map>

#include "Common/CompatVtableInstance.h"
#include "Common/Log.h"
#include "Common/VtableVisitor.h"
#include "Config/Config.h"
#include "D3dDdi/ScopedCriticalSection.h"

namespace D3dDdi
{
	template <typename Vtable>
	class D3dDdiVtable
	{
	public:
		static void hookVtable(HMODULE module, const Vtable* vtable)
		{
			if (!vtable)
			{
				return;
			}

			auto it = s_origModuleVtables.find(module);
			if (s_origModuleVtables.end() == it)
			{
				it = s_origModuleVtables.emplace(module, hookVtableInstance(*vtable, InstanceId<0>())).first;
			}
		}

		static std::map<HMODULE, const Vtable&> s_origModuleVtables;
		static Vtable*& s_origVtablePtr;

	private:
		template <int instanceId>
		class Visitor
		{
		public:
			Visitor(Vtable& compatVtable)
				: m_compatVtable(compatVtable)
			{
			}

			template <typename MemberDataPtr, MemberDataPtr ptr>
			void visit(const char* /*funcName*/)
			{
				if (!(m_compatVtable.*ptr))
				{
					m_compatVtable.*ptr = &threadSafeFunc<MemberDataPtr, ptr>;
				}
			}

		private:
			template <typename MemberDataPtr, MemberDataPtr ptr, typename Result, typename... Params>
			static Result APIENTRY threadSafeFunc(Params... params)
			{
				D3dDdi::ScopedCriticalSection lock;
				return (CompatVtableInstance<Vtable, instanceId>::s_origVtable.*ptr)(params...);
			}

			Vtable& m_compatVtable;
		};

		template <int instanceId> struct InstanceId {};

		template <int instanceId>
		static const Vtable& hookVtableInstance(const Vtable& vtable, InstanceId<instanceId>)
		{
			static bool isHooked = false;
			if (isHooked)
			{
				return hookVtableInstance(vtable, InstanceId<instanceId + 1>());
			}

			Vtable compatVtable = {};
			Compat::setCompatVtable(compatVtable);

#ifndef DEBUGLOGS
			Visitor<instanceId> visitor(compatVtable);
			forEach<Vtable>(visitor);
#endif

			isHooked = true;
			CompatVtableInstance<Vtable, instanceId>::hookVtable(vtable, compatVtable);
			return CompatVtableInstance<Vtable, instanceId>::s_origVtable;
		}

		static const Vtable& hookVtableInstance(const Vtable& /*vtable*/, InstanceId<Config::maxUserModeDisplayDrivers>)
		{
			Compat::Log() << "ERROR: Cannot hook more than " << Config::maxUserModeDisplayDrivers <<
				" user-mode display drivers. Recompile with Config::maxUserModeDisplayDrivers > " <<
				Config::maxUserModeDisplayDrivers << '.';
			static Vtable vtable = {};
			return vtable;
		}
	};

	template <typename Vtable>
	std::map<HMODULE, const Vtable&> D3dDdiVtable<Vtable>::s_origModuleVtables;

	template <typename Vtable>
	Vtable*& D3dDdiVtable<Vtable>::s_origVtablePtr = CompatVtableInstanceBase<Vtable>::s_origVtablePtr;
}
