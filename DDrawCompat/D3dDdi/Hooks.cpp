#define CINTERFACE

#include <Windows.h>
#include <d3d.h>
#include <d3dumddi.h>
#include <..\km\d3dkmthk.h>

#include "D3dDdi/AdapterCallbacks.h"
#include "D3dDdi/AdapterFuncs.h"
#include "DDrawLog.h"
#include "Hook.h"

HRESULT APIENTRY OpenAdapter(D3DDDIARG_OPENADAPTER*) { return 0; }

std::ostream& operator<<(std::ostream& os, const D3DDDIARG_OPENADAPTER& data)
{
	return Compat::LogStruct(os)
		<< data.hAdapter
		<< data.Interface
		<< data.Version
		<< data.pAdapterCallbacks
		<< data.pAdapterFuncs
		<< data.DriverVersion;
}

namespace
{
	UINT g_ddiVersion = 0;
	HMODULE g_umd = nullptr;
	
	D3DKMT_HANDLE openAdapterFromHdc(HDC hdc);

	void closeAdapter(D3DKMT_HANDLE adapter)
	{
		D3DKMT_CLOSEADAPTER closeAdapterData = {};
		closeAdapterData.hAdapter = adapter;
		D3DKMTCloseAdapter(&closeAdapterData);
	}

	DISPLAY_DEVICE getPrimaryDisplayDevice()
	{
		DISPLAY_DEVICE dd = {};
		dd.cb = sizeof(dd);
		for (DWORD i = 0;
			EnumDisplayDevices(nullptr, i, &dd, 0) && !(dd.StateFlags & DISPLAY_DEVICE_PRIMARY_DEVICE);
			++i)
		{
		}

		if (!(dd.StateFlags & DISPLAY_DEVICE_PRIMARY_DEVICE))
		{
			Compat::Log() << "Failed to find the primary display device";
			ZeroMemory(&dd, sizeof(dd));
		}

		return dd;
	}

	D3DKMT_UMDFILENAMEINFO getUmdDriverName(D3DKMT_HANDLE adapter)
	{
		D3DKMT_UMDFILENAMEINFO umdFileNameInfo = {};
		umdFileNameInfo.Version = KMTUMDVERSION_DX9;

		D3DKMT_QUERYADAPTERINFO queryAdapterInfo = {};
		queryAdapterInfo.hAdapter = adapter;
		queryAdapterInfo.Type = KMTQAITYPE_UMDRIVERNAME;
		queryAdapterInfo.pPrivateDriverData = &umdFileNameInfo;
		queryAdapterInfo.PrivateDriverDataSize = sizeof(umdFileNameInfo);
		NTSTATUS result = D3DKMTQueryAdapterInfo(&queryAdapterInfo);
		if (FAILED(result))
		{
			Compat::Log() << "Failed to query the display driver name: " << result;
			ZeroMemory(&umdFileNameInfo, sizeof(umdFileNameInfo));
		}

		return umdFileNameInfo;
	}

	D3DKMT_UMDFILENAMEINFO getPrimaryUmdDriverName()
	{
		D3DKMT_UMDFILENAMEINFO umdFileNameInfo = {};

		DISPLAY_DEVICE dd = getPrimaryDisplayDevice();
		if (!dd.DeviceName)
		{
			return umdFileNameInfo;
		}

		HDC dc = CreateDC(nullptr, dd.DeviceName, nullptr, nullptr);
		if (!dc)
		{
			Compat::Log() << "Failed to create a DC for the primary display device";
			return umdFileNameInfo;
		}

		D3DKMT_HANDLE adapter = openAdapterFromHdc(dc);
		DeleteDC(dc);
		if (!adapter)
		{
			return umdFileNameInfo;
		}

		umdFileNameInfo = getUmdDriverName(adapter);
		closeAdapter(adapter);
		if (0 == umdFileNameInfo.UmdFileName[0])
		{
			return umdFileNameInfo;
		}

		Compat::Log() << "Primary display adapter driver: " << umdFileNameInfo.UmdFileName;
		return umdFileNameInfo;
	}

	HRESULT APIENTRY openAdapter(D3DDDIARG_OPENADAPTER* pOpenData)
	{
		Compat::LogEnter("openAdapter", pOpenData);
		D3dDdi::AdapterCallbacks::hookVtable(pOpenData->pAdapterCallbacks);
		HRESULT result = CALL_ORIG_FUNC(OpenAdapter)(pOpenData);
		if (SUCCEEDED(result))
		{
			g_ddiVersion = min(pOpenData->Version, pOpenData->DriverVersion);
			D3dDdi::AdapterFuncs::hookVtable(pOpenData->pAdapterFuncs);
		}
		Compat::LogLeave("openAdapter", pOpenData) << result;
		return result;
	}

	D3DKMT_HANDLE openAdapterFromHdc(HDC hdc)
	{
		D3DKMT_OPENADAPTERFROMHDC openAdapterData = {};
		openAdapterData.hDc = hdc;
		NTSTATUS result = D3DKMTOpenAdapterFromHdc(&openAdapterData);
		if (FAILED(result))
		{
			Compat::Log() << "Failed to open the primary display adapter: " << result;
			return 0;
		}
		return openAdapterData.hAdapter;
	}
}

namespace D3dDdi
{
	UINT getDdiVersion()
	{
		return g_ddiVersion;
	}

	void installHooks()
	{
		D3DKMT_UMDFILENAMEINFO primaryUmd = getPrimaryUmdDriverName();
		g_umd = LoadLibraryW(primaryUmd.UmdFileName);
		if (!g_umd)
		{
			Compat::Log() << "Failed to load the primary display driver library";
		}

		char umdFileName[MAX_PATH] = {};
		wcstombs_s(nullptr, umdFileName, primaryUmd.UmdFileName, _TRUNCATE);
		Compat::hookFunction<decltype(&OpenAdapter), &OpenAdapter>(
			umdFileName, "OpenAdapter", &openAdapter);
	}

	void uninstallHooks()
	{
		if (g_umd)
		{
			FreeLibrary(g_umd);
		}
	}
}
