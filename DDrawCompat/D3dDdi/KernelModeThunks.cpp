#include <atomic>
#include <map>
#include <string>

#include <d3d.h>
#include <d3dumddi.h>
#include <../km/d3dkmthk.h>

#include "Common/Log.h"
#include "Common/Hook.h"
#include "Common/ScopedCriticalSection.h"
#include "Common/Time.h"
#include "D3dDdi/Hooks.h"
#include "D3dDdi/KernelModeThunks.h"
#include "D3dDdi/Log/KernelModeThunksLog.h"
#include "DDraw/Surfaces/PrimarySurface.h"
#include "Win32/DisplayMode.h"

namespace
{
	struct AdapterInfo
	{
		UINT adapter;
		UINT vidPnSourceId;
		RECT monitorRect;
	};

	struct ContextInfo
	{
		D3DKMT_HANDLE device;

		ContextInfo() : device(0) {}
	};

	std::map<D3DKMT_HANDLE, ContextInfo> g_contexts;
	AdapterInfo g_gdiAdapterInfo = {};
	AdapterInfo g_lastOpenAdapterInfo = {};
	std::string g_lastDDrawCreateDcDevice;
	UINT g_lastFlipInterval = 0;
	UINT g_flipIntervalOverride = 0;
	D3DKMT_HANDLE g_lastPresentContext = 0;
	UINT g_presentCount = 0;
	std::atomic<long long> g_qpcLastVerticalBlank = 0;
	Compat::CriticalSection g_vblankCs;

	decltype(D3DKMTCreateContextVirtual)* g_origD3dKmtCreateContextVirtual = nullptr;

	NTSTATUS APIENTRY closeAdapter(const D3DKMT_CLOSEADAPTER* pData)
	{
		Compat::ScopedCriticalSection lock(g_vblankCs);
		if (pData && pData->hAdapter == g_lastOpenAdapterInfo.adapter)
		{
			g_lastOpenAdapterInfo = {};
		}
		return CALL_ORIG_FUNC(D3DKMTCloseAdapter)(pData);
	}

	NTSTATUS APIENTRY createContext(D3DKMT_CREATECONTEXT* pData)
	{
		LOG_FUNC("D3DKMTCreateContext", pData);
		NTSTATUS result = CALL_ORIG_FUNC(D3DKMTCreateContext)(pData);
		if (SUCCEEDED(result))
		{
			g_contexts[pData->hContext].device = pData->hDevice;
		}
		return LOG_RESULT(result);
	}

	NTSTATUS APIENTRY createContextVirtual(D3DKMT_CREATECONTEXTVIRTUAL* pData)
	{
		LOG_FUNC("D3DKMTCreateContextVirtual", pData);
		NTSTATUS result = g_origD3dKmtCreateContextVirtual(pData);
		if (SUCCEEDED(result))
		{
			g_contexts[pData->hContext].device = pData->hDevice;
		}
		return LOG_RESULT(result);
	}

	NTSTATUS APIENTRY createDcFromMemory(D3DKMT_CREATEDCFROMMEMORY* pData)
	{
		LOG_FUNC("D3DKMTCreateDCFromMemory", pData);
		NTSTATUS result = 0;
		if (pData && D3DDDIFMT_P8 == pData->Format && !pData->pColorTable &&
			DDraw::PrimarySurface::s_palette)
		{
			pData->pColorTable = DDraw::PrimarySurface::s_paletteEntries;
			result = CALL_ORIG_FUNC(D3DKMTCreateDCFromMemory)(pData);
			pData->pColorTable = nullptr;
		}
		else
		{
			result = CALL_ORIG_FUNC(D3DKMTCreateDCFromMemory)(pData);
		}
		return LOG_RESULT(result);
	}

	NTSTATUS APIENTRY createDevice(D3DKMT_CREATEDEVICE* pData)
	{
		LOG_FUNC("D3DKMTCreateDevice", pData);
		NTSTATUS result = CALL_ORIG_FUNC(D3DKMTCreateDevice)(pData);
		if (SUCCEEDED(result))
		{
			D3DKMT_SETQUEUEDLIMIT limit = {};
			limit.hDevice = pData->hDevice;
			limit.Type = D3DKMT_SET_QUEUEDLIMIT_PRESENT;
			limit.QueuedPresentLimit = 2;
			CALL_ORIG_FUNC(D3DKMTSetQueuedLimit)(&limit);
		}
		return LOG_RESULT(result);
	}

	HDC WINAPI ddrawCreateDcA(LPCSTR pwszDriver, LPCSTR pwszDevice, LPCSTR pszPort, const DEVMODEA* pdm)
	{
		LOG_FUNC("ddrawCreateDCA", pwszDriver, pwszDevice, pszPort, pdm);
		g_lastDDrawCreateDcDevice = pwszDevice ? pwszDevice : std::string();
		return LOG_RESULT(CALL_ORIG_FUNC(CreateDCA)(pwszDriver, pwszDevice, pszPort, pdm));
	}

	NTSTATUS APIENTRY destroyContext(const D3DKMT_DESTROYCONTEXT* pData)
	{
		LOG_FUNC("D3DKMTDestroyContext", pData);
		NTSTATUS result = CALL_ORIG_FUNC(D3DKMTDestroyContext)(pData);
		if (SUCCEEDED(result))
		{
			g_contexts.erase(pData->hContext);
			if (g_lastPresentContext == pData->hContext)
			{
				g_lastPresentContext = 0;
			}
		}
		return LOG_RESULT(result);
	}

	BOOL CALLBACK findDDrawMonitorRect(HMONITOR hMonitor, HDC /*hdcMonitor*/, LPRECT /*lprcMonitor*/, LPARAM dwData)
	{
		MONITORINFOEX mi = {};
		mi.cbSize = sizeof(mi);
		GetMonitorInfo(hMonitor, &mi);
		if (g_lastDDrawCreateDcDevice == mi.szDevice)
		{
			*reinterpret_cast<RECT*>(dwData) = mi.rcMonitor;
			return FALSE;
		}
		return TRUE;
	}

	AdapterInfo getAdapterInfo(const D3DKMT_OPENADAPTERFROMHDC& data)
	{
		AdapterInfo adapterInfo = {};
		adapterInfo.adapter = data.hAdapter;
		adapterInfo.vidPnSourceId = data.VidPnSourceId;

		EnumDisplayMonitors(nullptr, nullptr, findDDrawMonitorRect,
			reinterpret_cast<LPARAM>(&adapterInfo.monitorRect));

		if (IsRectEmpty(&adapterInfo.monitorRect))
		{
			MONITORINFO mi = {};
			mi.cbSize = sizeof(mi);
			GetMonitorInfo(MonitorFromPoint({}, MONITOR_DEFAULTTOPRIMARY), &mi);
			adapterInfo.monitorRect = mi.rcMonitor;
		}

		return adapterInfo;
	}

	NTSTATUS APIENTRY openAdapterFromHdc(D3DKMT_OPENADAPTERFROMHDC* pData)
	{
		LOG_FUNC("D3DKMTOpenAdapterFromHdc", pData);
		NTSTATUS result = CALL_ORIG_FUNC(D3DKMTOpenAdapterFromHdc)(pData);
		if (SUCCEEDED(result))
		{
			Compat::ScopedCriticalSection lock(g_vblankCs);
			g_lastOpenAdapterInfo = getAdapterInfo(*pData);
		}
		return LOG_RESULT(result);
	}

	NTSTATUS APIENTRY present(D3DKMT_PRESENT* pData)
	{
		LOG_FUNC("D3DKMTPresent", pData);

		if (pData->Flags.Flip)
		{
			g_lastFlipInterval = pData->FlipInterval;
			g_lastPresentContext = pData->hContext;

			if (UINT_MAX == g_flipIntervalOverride)
			{
				return LOG_RESULT(S_OK);
			}

			++g_presentCount;
			if (0 == g_presentCount)
			{
				g_presentCount = 1;
			}

			pData->PresentCount = g_presentCount;
			pData->Flags.PresentCountValid = 1;
			pData->FlipInterval = static_cast<D3DDDI_FLIPINTERVAL_TYPE>(g_flipIntervalOverride);
		}

		return LOG_RESULT(CALL_ORIG_FUNC(D3DKMTPresent)(pData));
	}

	NTSTATUS APIENTRY queryAdapterInfo(const D3DKMT_QUERYADAPTERINFO* pData)
	{
		LOG_FUNC("D3DKMTQueryAdapterInfo", pData);
		NTSTATUS result = CALL_ORIG_FUNC(D3DKMTQueryAdapterInfo)(pData);
		if (SUCCEEDED(result) && KMTQAITYPE_UMDRIVERNAME == pData->Type)
		{
			auto info = static_cast<D3DKMT_UMDFILENAMEINFO*>(pData->pPrivateDriverData);
			D3dDdi::onUmdFileNameQueried(info->UmdFileName);
		}
		return LOG_RESULT(result);
	}

	NTSTATUS APIENTRY setQueuedLimit(const D3DKMT_SETQUEUEDLIMIT* pData)
	{
		LOG_FUNC("D3DKMTSetQueuedLimit", pData);
		if (D3DKMT_SET_QUEUEDLIMIT_PRESENT == pData->Type)
		{
			const UINT origLimit = pData->QueuedPresentLimit;
			const_cast<D3DKMT_SETQUEUEDLIMIT*>(pData)->QueuedPresentLimit = 2;
			NTSTATUS result = CALL_ORIG_FUNC(D3DKMTSetQueuedLimit)(pData);
			const_cast<D3DKMT_SETQUEUEDLIMIT*>(pData)->QueuedPresentLimit = origLimit;
			return LOG_RESULT(result);
		}
		return LOG_RESULT(CALL_ORIG_FUNC(D3DKMTSetQueuedLimit)(pData));
	}

	void updateGdiAdapterInfo()
	{
		static auto lastDisplaySettingsUniqueness = Win32::DisplayMode::queryDisplaySettingsUniqueness() - 1;
		const auto currentDisplaySettingsUniqueness = Win32::DisplayMode::queryDisplaySettingsUniqueness();
		if (currentDisplaySettingsUniqueness != lastDisplaySettingsUniqueness)
		{
			if (g_gdiAdapterInfo.adapter)
			{
				D3DKMT_CLOSEADAPTER data = {};
				data.hAdapter = g_gdiAdapterInfo.adapter;
				CALL_ORIG_FUNC(D3DKMTCloseAdapter)(&data);
				g_gdiAdapterInfo = {};
			}

			D3DKMT_OPENADAPTERFROMHDC data = {};
			data.hDc = GetDC(nullptr);
			if (SUCCEEDED(CALL_ORIG_FUNC(D3DKMTOpenAdapterFromHdc)(&data)))
			{
				g_gdiAdapterInfo = getAdapterInfo(data);
			}
			CALL_ORIG_FUNC(ReleaseDC)(nullptr, data.hDc);

			lastDisplaySettingsUniqueness = currentDisplaySettingsUniqueness;
		}
	}
}

namespace D3dDdi
{
	namespace KernelModeThunks
	{
		UINT getLastFlipInterval()
		{
			return g_lastFlipInterval;
		}

		UINT getLastDisplayedFrameCount()
		{
			auto contextIter = g_contexts.find(g_lastPresentContext);
			if (contextIter == g_contexts.end())
			{
				return g_presentCount;
			}

			D3DKMT_GETDEVICESTATE data = {};
			data.hDevice = contextIter->second.device;
			data.StateType = D3DKMT_DEVICESTATE_PRESENT;
			data.PresentState.VidPnSourceId = g_lastOpenAdapterInfo.vidPnSourceId;
			D3DKMTGetDeviceState(&data);

			if (0 == data.PresentState.PresentStats.PresentCount)
			{
				return g_presentCount;
			}

			return data.PresentState.PresentStats.PresentCount;
		}

		UINT getLastSubmittedFrameCount()
		{
			return g_presentCount;
		}

		RECT getMonitorRect()
		{
			auto primary(DDraw::PrimarySurface::getPrimary());
			if (!primary)
			{
				return {};
			}

			static auto lastDisplaySettingsUniqueness = Win32::DisplayMode::queryDisplaySettingsUniqueness() - 1;
			const auto currentDisplaySettingsUniqueness = Win32::DisplayMode::queryDisplaySettingsUniqueness();
			if (currentDisplaySettingsUniqueness != lastDisplaySettingsUniqueness)
			{
				lastDisplaySettingsUniqueness = currentDisplaySettingsUniqueness;
				CompatPtr<IUnknown> ddUnk;
				primary.get()->lpVtbl->GetDDInterface(primary, reinterpret_cast<void**>(&ddUnk.getRef()));
				CompatPtr<IDirectDraw7> dd7(ddUnk);

				DDDEVICEIDENTIFIER2 di = {};
				dd7->GetDeviceIdentifier(dd7, &di, 0);
			}

			return g_lastOpenAdapterInfo.monitorRect;
		}

		long long getQpcLastVerticalBlank()
		{
			return g_qpcLastVerticalBlank;
		}

		void installHooks(HMODULE origDDrawModule)
		{
			HOOK_FUNCTION(gdi32, D3DKMTCloseAdapter, closeAdapter);
			HOOK_FUNCTION(gdi32, D3DKMTCreateContext, createContext);
			HOOK_FUNCTION(gdi32, D3DKMTCreateDevice, createDevice);
			HOOK_FUNCTION(gdi32, D3DKMTCreateDCFromMemory, createDcFromMemory);
			HOOK_FUNCTION(gdi32, D3DKMTDestroyContext, destroyContext);
			HOOK_FUNCTION(gdi32, D3DKMTOpenAdapterFromHdc, openAdapterFromHdc);
			HOOK_FUNCTION(gdi32, D3DKMTQueryAdapterInfo, queryAdapterInfo);
			HOOK_FUNCTION(gdi32, D3DKMTPresent, present);
			HOOK_FUNCTION(gdi32, D3DKMTSetQueuedLimit, setQueuedLimit);
			Compat::hookIatFunction(origDDrawModule, "gdi32.dll", "CreateDCA", ddrawCreateDcA);

			// Functions not available in Windows Vista
			Compat::hookFunction("gdi32", "D3DKMTCreateContextVirtual",
				reinterpret_cast<void*&>(g_origD3dKmtCreateContextVirtual), createContextVirtual);
		}

		void setFlipIntervalOverride(UINT flipInterval)
		{
			g_flipIntervalOverride = flipInterval;
		}

		void waitForVerticalBlank()
		{
			D3DKMT_WAITFORVERTICALBLANKEVENT data = {};

			{
				Compat::ScopedCriticalSection lock(g_vblankCs);
				if (g_lastOpenAdapterInfo.adapter)
				{
					data.hAdapter = g_lastOpenAdapterInfo.adapter;
					data.VidPnSourceId = g_lastOpenAdapterInfo.vidPnSourceId;
				}
				else
				{
					updateGdiAdapterInfo();
					data.hAdapter = g_gdiAdapterInfo.adapter;
					data.VidPnSourceId = g_gdiAdapterInfo.vidPnSourceId;
				}
			}
			
			if (data.hAdapter)
			{
				D3DKMTWaitForVerticalBlankEvent(&data);
				g_qpcLastVerticalBlank = Time::queryPerformanceCounter();
			}
		}
	}
}
