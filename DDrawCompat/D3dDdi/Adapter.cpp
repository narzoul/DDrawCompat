#include "D3dDdi/Adapter.h"
#include "D3dDdi/AdapterFuncs.h"

namespace D3dDdi
{
	Adapter::Adapter(HANDLE adapter, HMODULE module)
		: m_adapter(adapter)
		, m_module(module)
		, m_d3dExtendedCaps{}
	{
		if (m_adapter)
		{
			D3DDDIARG_GETCAPS getCaps = {};
			getCaps.Type = D3DDDICAPS_GETD3D7CAPS;
			getCaps.pData = &m_d3dExtendedCaps;
			getCaps.DataSize = sizeof(m_d3dExtendedCaps);
			D3dDdi::AdapterFuncs::s_origVtables.at(adapter).pfnGetCaps(adapter, &getCaps);
		}
	}

	void Adapter::add(HANDLE adapter, HMODULE module)
	{
		s_adapters.emplace(adapter, Adapter(adapter, module));
	}

	Adapter& Adapter::get(HANDLE adapter)
	{
		auto it = s_adapters.find(adapter);
		if (it != s_adapters.end())
		{
			return it->second;
		}

		return s_adapters.emplace(adapter, Adapter(adapter, nullptr)).first->second;
	}

	void Adapter::remove(HANDLE adapter)
	{
		s_adapters.erase(adapter);
	}

	std::map<HANDLE, Adapter> Adapter::s_adapters;
}
