#pragma once

#include <map>

#include "Common/CompatVtableInstance.h"
#include "Common/Log.h"
#include "Common/VtableVisitor.h"
#include "Config/Config.h"

namespace D3dDdi
{
	template <typename Vtable>
	class D3dDdiVtable
	{
	public:
		static void hookVtable(HMODULE module, HANDLE context, const Vtable* vtable)
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

			s_origVtables.emplace(context, it->second);
		}

		static std::map<HMODULE, const Vtable&> s_origModuleVtables;
		static std::map<HANDLE, const Vtable&> s_origVtables;

	private:
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
	std::map<HANDLE, const Vtable&> D3dDdiVtable<Vtable>::s_origVtables;
}
