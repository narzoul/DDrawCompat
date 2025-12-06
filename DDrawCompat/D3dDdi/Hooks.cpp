#include <functional>
#include <set>

#include <Windows.h>
#include <VersionHelpers.h>
#include <winternl.h>
#include <d3dkmthk.h>

#include <Common/Hook.h>
#include <Common/Log.h>
#include <D3dDdi/Adapter.h>
#include <D3dDdi/AdapterCallbacks.h>
#include <D3dDdi/AdapterFuncs.h>
#include <D3dDdi/Hooks.h>
#include <D3dDdi/KernelModeThunks.h>
#include <D3dDdi/ScopedCriticalSection.h>
#include <D3dDdi/Log/KernelModeThunksLog.h>
#include <Dll/Dll.h>

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
	struct D3D9ON12_CREATE_DEVICE_ARGS {};

	typedef HRESULT(APIENTRY* PFND3D9ON12_OPENADAPTER)(
		D3DDDIARG_OPENADAPTER* pOpenAdapter, LUID* pLUID, D3D9ON12_CREATE_DEVICE_ARGS* pArgs);
	typedef HRESULT(APIENTRY* PFND3D9ON12_KMTPRESENT)(
		HANDLE hDevice, D3DKMT_PRESENT* pKMTArgs);

	struct D3D9ON12_PRIVATE_DDI_TABLE
	{
		PFND3D9ON12_OPENADAPTER pfnOpenAdapter;
		FARPROC pfnGetSharedGDIHandle;
		FARPROC pfnCreateSharedNTHandle;
		FARPROC pfnGetDeviceState;
		PFND3D9ON12_KMTPRESENT pfnKMTPresent;
	};

	void APIENTRY getPrivateDdiTable(D3D9ON12_PRIVATE_DDI_TABLE* pPrivateDDITable);
	HRESULT APIENTRY kmtPresent(HANDLE hDevice, D3DKMT_PRESENT* pKMTArgs);
	HRESULT APIENTRY openAdapter(D3DDDIARG_OPENADAPTER* pOpenData);
	HRESULT APIENTRY openAdapterPrivate(D3DDDIARG_OPENADAPTER* pOpenData, LUID* pLUID, D3D9ON12_CREATE_DEVICE_ARGS* pArgs);

	decltype(&getPrivateDdiTable) g_origGetPrivateDdiTable = nullptr;
	PFND3DDDI_OPENADAPTER g_origOpenAdapter = nullptr;
	PFND3D9ON12_OPENADAPTER g_origOpenAdapterPrivate = nullptr;
	PFND3D9ON12_KMTPRESENT g_origKmtPresent = nullptr;

	void APIENTRY getPrivateDdiTable(D3D9ON12_PRIVATE_DDI_TABLE* pPrivateDDITable)
	{
		LOG_FUNC("GetPrivateDDITable", pPrivateDDITable);
		g_origGetPrivateDdiTable(pPrivateDDITable);
		g_origOpenAdapterPrivate = pPrivateDDITable->pfnOpenAdapter;
		g_origKmtPresent = pPrivateDDITable->pfnKMTPresent;
		pPrivateDDITable->pfnOpenAdapter = &openAdapterPrivate;
		pPrivateDDITable->pfnKMTPresent = &kmtPresent;
	}

	FARPROC WINAPI getProcAddress(HMODULE hModule, LPCSTR lpProcName)
	{
		LOG_FUNC("GetProcAddress", hModule, lpProcName);
		if (lpProcName)
		{
			if ("OpenAdapter" == std::string(lpProcName))
			{
				g_origOpenAdapter = reinterpret_cast<PFND3DDDI_OPENADAPTER>(
					GetProcAddress(hModule, lpProcName));
				return g_origOpenAdapter ? reinterpret_cast<FARPROC>(&openAdapter) : nullptr;
			}
			else if ("GetPrivateDDITable" == std::string(lpProcName))
			{
				g_origGetPrivateDdiTable = reinterpret_cast<decltype(&getPrivateDdiTable)>(
					GetProcAddress(hModule, lpProcName));
				return g_origGetPrivateDdiTable ? reinterpret_cast<FARPROC>(&getPrivateDdiTable) : nullptr;
			}
		}
		return LOG_RESULT(GetProcAddress(hModule, lpProcName));
	}

	HRESULT APIENTRY kmtPresent(HANDLE hDevice, D3DKMT_PRESENT* pKMTArgs)
	{
		LOG_FUNC("KMTPresent", hDevice, pKMTArgs);
		D3dDdi::KernelModeThunks::fixPresent(*pKMTArgs);
		return LOG_RESULT(g_origKmtPresent(hDevice, pKMTArgs));
	}

	void logUmdInfo(D3DKMT_HANDLE adapter)
	{
		static std::set<HMODULE> hookedModules;
		HMODULE driverModule = Compat::getModuleHandleFromAddress(g_origOpenAdapter);
		if (hookedModules.find(driverModule) != hookedModules.end())
		{
			return;
		}

		LOG_INFO << "Hooking user mode display driver: " << Compat::funcPtrToStr(g_origOpenAdapter);
		Dll::pinModule(driverModule);
		hookedModules.insert(driverModule);

		D3DKMT_QUERYADAPTERINFO data = {};
		data.hAdapter = adapter;

		if (GetModuleHandle("d3d9on12") == driverModule)
		{
			D3DKMT_UMDFILENAMEINFO fn = {};
			fn.Version = KMTUMDVERSION_DX12;
			data.Type = KMTQAITYPE_UMDRIVERNAME;
			data.pPrivateDriverData = &fn;
			data.PrivateDriverDataSize = sizeof(fn);
			if (SUCCEEDED(D3DKMTQueryAdapterInfo(&data)))
			{
				LOG_INFO << "DX12 driver: " << fn.UmdFileName;
			}
		}

		D3DKMT_UMD_DRIVER_VERSION version = {};
		data.Type = KMTQAITYPE_UMD_DRIVER_VERSION;
		data.pPrivateDriverData = &version;
		data.PrivateDriverDataSize = sizeof(version);
		if (IsWindows8OrGreater() && SUCCEEDED(D3DKMTQueryAdapterInfo(&data)))
		{
			const auto ver = reinterpret_cast<const WORD*>(&version.DriverVersion.QuadPart);
			LOG_INFO << "Driver version: " << ver[3] << '.' << ver[2] << '.' << ver[1] << '.' << ver[0];
		}

		D3DKMT_ADAPTERREGISTRYINFO ri = {};
		data.Type = KMTQAITYPE_ADAPTERREGISTRYINFO;
		data.pPrivateDriverData = &ri;
		data.PrivateDriverDataSize = sizeof(ri);
		if (SUCCEEDED(D3DKMTQueryAdapterInfo(&data)))
		{
			LOG_INFO << "Adapter name: " << ri.AdapterString;
		}
	}

	HRESULT openAdapterCommon(D3DDDIARG_OPENADAPTER* pOpenData, std::function<HRESULT()> origOpenAdapter)
	{
		if (pOpenData->Interface > 7)
		{
			return origOpenAdapter();
		}

		D3dDdi::ScopedCriticalSection lock;
		D3dDdi::AdapterCallbacks::hookVtable(*pOpenData->pAdapterCallbacks, pOpenData->Version);
		auto origAdapter = pOpenData->hAdapter;
		auto origInterface = pOpenData->Interface;
		pOpenData->Interface = 9;
		HRESULT result = origOpenAdapter();
		pOpenData->Interface = origInterface;
		if (SUCCEEDED(result))
		{
			logUmdInfo(reinterpret_cast<D3DKMT_HANDLE>(origAdapter));
			UINT version = std::min(pOpenData->Version, pOpenData->DriverVersion);
			if (0 == D3dDdi::g_umdVersion || version < D3dDdi::g_umdVersion)
			{
				D3dDdi::g_umdVersion = version;
			}
			D3dDdi::AdapterFuncs::hookVtable(*pOpenData->pAdapterFuncs, version);
			D3dDdi::Adapter::add(*pOpenData);
		}
		return result;
	}

	HRESULT APIENTRY openAdapter(D3DDDIARG_OPENADAPTER* pOpenData)
	{
		LOG_FUNC("OpenAdapter", pOpenData);
		return LOG_RESULT(openAdapterCommon(pOpenData, [=]() { return g_origOpenAdapter(pOpenData); }));
	}

	HRESULT APIENTRY openAdapterPrivate(D3DDDIARG_OPENADAPTER* pOpenData, LUID* pLUID, D3D9ON12_CREATE_DEVICE_ARGS* pArgs)
	{
		LOG_FUNC("OpenAdapter_Private", pOpenData, pLUID, pArgs);
		return LOG_RESULT(openAdapterCommon(pOpenData, [=]() { return g_origOpenAdapterPrivate(pOpenData, pLUID, pArgs); }));
	}
}

namespace D3dDdi
{
	void installHooks()
	{
		LOG_INFO << "Installing Direct3D driver hooks";
		Compat::hookIatFunction(Dll::g_origDDrawModule, "GetProcAddress", getProcAddress);
		KernelModeThunks::installHooks();
	}
}
