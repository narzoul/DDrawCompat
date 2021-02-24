#pragma once

#include <Common/CompatVtableInstance.h>
#include <Common/Log.h>
#include <Common/VtableVisitor.h>
#include <Config/Config.h>
#include <D3dDdi/ScopedCriticalSection.h>

namespace D3dDdi
{
	template <typename Vtable>
	class D3dDdiVtable
	{
	public:
		static void hookVtable(HMODULE module, const Vtable* vtable)
		{
			if (vtable)
			{
				hookVtableInstance(module, *vtable, InstanceId<0>());
			}
		}

		static Vtable*& s_origVtablePtr;

	private:
		class CopyVisitor
		{
		public:
			CopyVisitor(Vtable& compatVtable, const Vtable& origVtable)
				: m_compatVtable(compatVtable)
				, m_origVtable(origVtable)
			{
			}

			template <typename MemberDataPtr, MemberDataPtr ptr>
			void visit(const char* /*funcName*/)
			{
				m_compatVtable.*ptr = m_origVtable.*ptr;
			}

		private:
			Vtable& m_compatVtable;
			const Vtable& m_origVtable;
		};

		template <int instanceId> struct InstanceId {};

		template <int instanceId>
		static const Vtable& hookVtableInstance(HMODULE module, const Vtable& vtable, InstanceId<instanceId>)
		{
			static HMODULE hookedModule = nullptr;
			if (hookedModule && hookedModule != module)
			{
				return hookVtableInstance(module, vtable, InstanceId<instanceId + 1>());
			}
			hookedModule = module;

			Vtable compatVtable = {};
			CopyVisitor copyVisitor(compatVtable, vtable);
			forEach<Vtable>(copyVisitor);

			Compat::setCompatVtable(compatVtable);

			CompatVtableInstance<Vtable, ScopedCriticalSection, instanceId>::hookVtable(vtable, compatVtable);
			return CompatVtableInstance<Vtable, ScopedCriticalSection, instanceId>::s_origVtable;
		}

		static const Vtable& hookVtableInstance(HMODULE /*module*/, const Vtable& /*vtable*/,
			InstanceId<Config::maxUserModeDisplayDrivers>)
		{
			Compat::Log() << "ERROR: Cannot hook more than " << Config::maxUserModeDisplayDrivers <<
				" user-mode display drivers. Recompile with Config::maxUserModeDisplayDrivers > " <<
				Config::maxUserModeDisplayDrivers << '.';
			static Vtable vtable = {};
			return vtable;
		}
	};

	template <typename Vtable>
	Vtable*& D3dDdiVtable<Vtable>::s_origVtablePtr = CompatVtableInstanceBase<Vtable>::s_origVtablePtr;
}
