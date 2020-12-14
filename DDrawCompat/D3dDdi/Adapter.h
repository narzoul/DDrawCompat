#pragma once

#include <map>

#include <d3d.h>
#include <d3dnthal.h>
#include <d3dumddi.h>

namespace D3dDdi
{
	class Adapter
	{
	public:
		Adapter(HANDLE adapter, HMODULE module);

		operator HANDLE() const { return m_adapter; }

		const DDRAW_CAPS& getDDrawCaps() const { return m_ddrawCaps; }
		const D3DNTHAL_D3DEXTENDEDCAPS& getD3dExtendedCaps() const { return m_d3dExtendedCaps; }
		HMODULE getModule() const { return m_module; }

		static void add(HANDLE adapter, HMODULE module);
		static Adapter& get(HANDLE adapter);
		static void remove(HANDLE adapter);

	private:
		HANDLE m_adapter;
		HMODULE m_module;
		D3DNTHAL_D3DEXTENDEDCAPS m_d3dExtendedCaps;
		DDRAW_CAPS m_ddrawCaps;

		static std::map<HANDLE, Adapter> s_adapters;
	};
}
