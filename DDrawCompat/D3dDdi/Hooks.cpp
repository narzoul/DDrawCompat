#include <set>

#include <d3d.h>
#include <d3dumddi.h>
#include <winternl.h>
#include <..\km\d3dkmthk.h>

#include "Common/Hook.h"
#include "Common/Log.h"
#include "D3dDdi/AdapterCallbacks.h"
#include "D3dDdi/AdapterFuncs.h"
#include "D3dDdi/KernelModeThunks.h"

std::ostream& operator<<(std::ostream& os, const D3DDDIARG_OPENADAPTER& data)
{
	return Compat::LogStruct(os)
		<< data.hAdapter
		<< data.Interface
		<< data.Version
		<< data.pAdapterCallbacks
		<< Compat::out(data.pAdapterFuncs)
		<< data.DriverVersion;
}

namespace
{
	UINT g_ddiVersion = 0;
	std::wstring g_hookedUmdFileName;
	HMODULE g_hookedUmdModule = nullptr;
	PFND3DDDI_OPENADAPTER g_origOpenAdapter = nullptr;

	void hookOpenAdapter(const std::wstring& umdFileName);
	HRESULT APIENTRY openAdapter(D3DDDIARG_OPENADAPTER* pOpenData);
	void unhookOpenAdapter();

	void hookOpenAdapter(const std::wstring& umdFileName)
	{
		g_hookedUmdFileName = umdFileName;
		g_hookedUmdModule = LoadLibraryW(umdFileName.c_str());
		if (g_hookedUmdModule)
		{
			Compat::hookFunction(g_hookedUmdModule, "OpenAdapter",
				reinterpret_cast<void*&>(g_origOpenAdapter), &openAdapter);
			FreeLibrary(g_hookedUmdModule);
		}
	}

	HRESULT APIENTRY openAdapter(D3DDDIARG_OPENADAPTER* pOpenData)
	{
		D3dDdi::ScopedCriticalSection lock;
		LOG_FUNC("openAdapter", pOpenData);
		D3dDdi::AdapterCallbacks::hookVtable(pOpenData->pAdapterCallbacks);
		HRESULT result = g_origOpenAdapter(pOpenData);
		if (SUCCEEDED(result))
		{
			static std::set<std::wstring> hookedUmdFileNames;
			if (hookedUmdFileNames.find(g_hookedUmdFileName) == hookedUmdFileNames.end())
			{
				Compat::Log() << "Hooking user mode display driver: " << g_hookedUmdFileName.c_str();
				hookedUmdFileNames.insert(g_hookedUmdFileName);
			}
			g_ddiVersion = min(pOpenData->Version, pOpenData->DriverVersion);
			D3dDdi::AdapterFuncs::hookVtable(g_hookedUmdModule, pOpenData->pAdapterFuncs);
			D3dDdi::AdapterFuncs::onOpenAdapter(pOpenData->hAdapter, g_hookedUmdModule);
		}
		return LOG_RESULT(result);
	}

	void unhookOpenAdapter()
	{
		if (g_origOpenAdapter)
		{
			Compat::unhookFunction(g_origOpenAdapter);
			g_hookedUmdFileName.clear();
		}
	}
}

namespace D3dDdi
{
	UINT getDdiVersion()
	{
		return g_ddiVersion;
	}

	void installHooks(HMODULE origDDrawModule)
	{
		KernelModeThunks::installHooks(origDDrawModule);
	}

	void onUmdFileNameQueried(const std::wstring& umdFileName)
	{
		if (g_hookedUmdFileName != umdFileName)
		{
			unhookOpenAdapter();
			hookOpenAdapter(umdFileName);
		}
	}

	void uninstallHooks()
	{
		unhookOpenAdapter();
	}
}
