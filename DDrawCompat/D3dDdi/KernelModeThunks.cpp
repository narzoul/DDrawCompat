#include <atomic>
#include <map>
#include <string>

#include <d3d.h>
#include <d3dumddi.h>
#include <winternl.h>
#include <../km/d3dkmthk.h>

#include <Common/Log.h>
#include <Common/Hook.h>
#include <Common/ScopedCriticalSection.h>
#include <Common/Time.h>
#include <D3dDdi/Device.h>
#include <D3dDdi/Hooks.h>
#include <D3dDdi/KernelModeThunks.h>
#include <D3dDdi/Log/KernelModeThunksLog.h>
#include <D3dDdi/Resource.h>
#include <D3dDdi/ScopedCriticalSection.h>
#include <DDraw/RealPrimarySurface.h>
#include <DDraw/ScopedThreadLock.h>
#include <DDraw/Surfaces/PrimarySurface.h>
#include <Gdi/Palette.h>
#include <Win32/DisplayMode.h>

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
	D3DDDIFORMAT g_dcFormatOverride = D3DDDIFMT_UNKNOWN;
	bool g_dcPaletteOverride = false;
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

	DWORD WINAPI waitForVsyncThreadProc(LPVOID lpParameter);

	NTSTATUS APIENTRY closeAdapter(const D3DKMT_CLOSEADAPTER* pData)
	{
		Compat::ScopedCriticalSection lock(g_vblankCs);
		if (pData && pData->hAdapter == g_lastOpenAdapterInfo.adapter)
		{
			g_lastOpenAdapterInfo = {};
		}
		return D3DKMTCloseAdapter(pData);
	}

	NTSTATUS APIENTRY createContext(D3DKMT_CREATECONTEXT* pData)
	{
		LOG_FUNC("D3DKMTCreateContext", pData);
		NTSTATUS result = D3DKMTCreateContext(pData);
		if (SUCCEEDED(result))
		{
			g_contexts[pData->hContext].device = pData->hDevice;
		}
		return LOG_RESULT(result);
	}

	NTSTATUS APIENTRY createContextVirtual(D3DKMT_CREATECONTEXTVIRTUAL* pData)
	{
		LOG_FUNC("D3DKMTCreateContextVirtual", pData);
		static auto d3dKmtCreateContextVirtual = reinterpret_cast<decltype(D3DKMTCreateContextVirtual)*>(
			GetProcAddress(GetModuleHandle("gdi32"), "D3DKMTCreateContextVirtual"));
		NTSTATUS result = d3dKmtCreateContextVirtual(pData);
		if (SUCCEEDED(result))
		{
			g_contexts[pData->hContext].device = pData->hDevice;
		}
		return LOG_RESULT(result);
	}

	NTSTATUS APIENTRY createDcFromMemory(D3DKMT_CREATEDCFROMMEMORY* pData)
	{
		LOG_FUNC("D3DKMTCreateDCFromMemory", pData);

		auto origFormat = pData->Format;
		if (D3DDDIFMT_UNKNOWN != g_dcFormatOverride)
		{
			pData->Format = g_dcFormatOverride;
		}

		std::vector<PALETTEENTRY> palette;
		auto origColorTable = pData->pColorTable;

		if (D3DDDIFMT_P8 == pData->Format)
		{
			if (g_dcPaletteOverride)
			{
				palette = Gdi::Palette::getHardwarePalette();
				pData->pColorTable = palette.data();
			}
			else
			{
				DDraw::ScopedThreadLock ddLock;
				D3dDdi::ScopedCriticalSection driverLock;
				auto primaryResource = D3dDdi::Device::findResource(DDraw::PrimarySurface::getFrontResource());
				if (primaryResource && pData->pMemory == primaryResource->getLockPtr(0) &&
					(DDraw::PrimarySurface::getOrigCaps() & DDSCAPS_COMPLEX))
				{
					pData->pColorTable = Gdi::Palette::getDefaultPalette();
				}
				else if (pData->pColorTable)
				{
					palette.assign(pData->pColorTable, pData->pColorTable + 256);
					auto sysPal = Gdi::Palette::getSystemPalette();
					for (UINT i = 0; i < 256; ++i)
					{
						if (palette[i].peFlags & PC_EXPLICIT)
						{
							palette[i] = sysPal[palette[i].peRed];
						}
					}
					pData->pColorTable = palette.data();
				}
				else
				{
					palette = Gdi::Palette::getHardwarePalette();
					pData->pColorTable = palette.data();
				}
			}
		}

		auto result = D3DKMTCreateDCFromMemory(pData);
		pData->Format = origFormat;
		pData->pColorTable = origColorTable;
		return LOG_RESULT(result);
	}

	HDC WINAPI ddrawCreateDcA(LPCSTR pwszDriver, LPCSTR pwszDevice, LPCSTR pszPort, const DEVMODEA* pdm)
	{
		LOG_FUNC("ddrawCreateDCA", pwszDriver, pwszDevice, pszPort, pdm);
		g_lastDDrawCreateDcDevice = pwszDevice ? pwszDevice : std::string();
		return LOG_RESULT(CreateDCA(pwszDriver, pwszDevice, pszPort, pdm));
	}

	NTSTATUS APIENTRY destroyContext(const D3DKMT_DESTROYCONTEXT* pData)
	{
		LOG_FUNC("D3DKMTDestroyContext", pData);
		NTSTATUS result = D3DKMTDestroyContext(pData);
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
		NTSTATUS result = D3DKMTOpenAdapterFromHdc(pData);
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

		return LOG_RESULT(D3DKMTPresent(pData));
	}

	NTSTATUS APIENTRY queryAdapterInfo(const D3DKMT_QUERYADAPTERINFO* pData)
	{
		LOG_FUNC("D3DKMTQueryAdapterInfo", pData);
		NTSTATUS result = D3DKMTQueryAdapterInfo(pData);
		if (SUCCEEDED(result))
		{
			switch (pData->Type)
			{
			case KMTQAITYPE_UMDRIVERNAME:
			{
				auto info = static_cast<D3DKMT_UMDFILENAMEINFO*>(pData->pPrivateDriverData);
				D3dDdi::onUmdFileNameQueried(info->UmdFileName);
			}
			break;

			case KMTQAITYPE_GETSEGMENTSIZE:
			{
				auto info = static_cast<D3DKMT_SEGMENTSIZEINFO*>(pData->pPrivateDriverData);
				const ULONGLONG maxMem = 0x3FFF0000;
				if (info->DedicatedVideoMemorySize > maxMem)
				{
					info->DedicatedVideoMemorySize = maxMem;
				}
				if (info->DedicatedVideoMemorySize + info->DedicatedSystemMemorySize > maxMem)
				{
					info->DedicatedSystemMemorySize = maxMem - info->DedicatedVideoMemorySize;
				}
				if (info->SharedSystemMemorySize > maxMem)
				{
					info->SharedSystemMemorySize = maxMem;
				}
			}
			break;
			}
		}
		return LOG_RESULT(result);
	}

	NTSTATUS APIENTRY setGammaRamp(const D3DKMT_SETGAMMARAMP* pData)
	{
		LOG_FUNC("D3DKMTSetGammaRamp", pData);
		HANDLE vsyncThread = CreateThread(nullptr, 0, &waitForVsyncThreadProc, nullptr, 0, nullptr);
		SetThreadPriority(vsyncThread, THREAD_PRIORITY_TIME_CRITICAL);
		DDraw::RealPrimarySurface::flush();
		HRESULT result = D3DKMTSetGammaRamp(pData);
		WaitForSingleObject(vsyncThread, INFINITE);
		CloseHandle(vsyncThread);
		return LOG_RESULT(result);
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
				D3DKMTCloseAdapter(&data);
				g_gdiAdapterInfo = {};
			}

			MONITORINFOEX mi = {};
			mi.cbSize = sizeof(mi);
			GetMonitorInfo(MonitorFromPoint({}, MONITOR_DEFAULTTOPRIMARY), &mi);

			D3DKMT_OPENADAPTERFROMHDC data = {};
			data.hDc = CreateDC(mi.szDevice, mi.szDevice, nullptr, nullptr);
			if (SUCCEEDED(D3DKMTOpenAdapterFromHdc(&data)))
			{
				g_gdiAdapterInfo = getAdapterInfo(data);
			}
			DeleteDC(data.hDc);

			lastDisplaySettingsUniqueness = currentDisplaySettingsUniqueness;
		}
	}

	DWORD WINAPI waitForVsyncThreadProc(LPVOID /*lpParameter*/)
	{
		D3dDdi::KernelModeThunks::waitForVerticalBlank();
		return 0;
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
				primary->GetDDInterface(primary, reinterpret_cast<void**>(&ddUnk.getRef()));
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
			Compat::hookIatFunction(origDDrawModule, "gdi32.dll", "CreateDCA", ddrawCreateDcA);
			Compat::hookIatFunction(origDDrawModule, "gdi32.dll", "D3DKMTCloseAdapter", closeAdapter);
			Compat::hookIatFunction(origDDrawModule, "gdi32.dll", "D3DKMTCreateContext", createContext);
			Compat::hookIatFunction(origDDrawModule, "gdi32.dll", "D3DKMTCreateContextVirtual", createContextVirtual);
			Compat::hookIatFunction(origDDrawModule, "gdi32.dll", "D3DKMTCreateDCFromMemory", createDcFromMemory);
			Compat::hookIatFunction(origDDrawModule, "gdi32.dll", "D3DKMTDestroyContext", destroyContext);
			Compat::hookIatFunction(origDDrawModule, "gdi32.dll", "D3DKMTOpenAdapterFromHdc", openAdapterFromHdc);
			Compat::hookIatFunction(origDDrawModule, "gdi32.dll", "D3DKMTQueryAdapterInfo", queryAdapterInfo);
			Compat::hookIatFunction(origDDrawModule, "gdi32.dll", "D3DKMTPresent", present);
			Compat::hookIatFunction(origDDrawModule, "gdi32.dll", "D3DKMTSetGammaRamp", setGammaRamp);
		}

		void setFlipIntervalOverride(UINT flipInterval)
		{
			g_flipIntervalOverride = flipInterval;
		}

		void setDcFormatOverride(UINT format)
		{
			g_dcFormatOverride = static_cast<D3DDDIFORMAT>(format);
		}

		void setDcPaletteOverride(bool enable)
		{
			g_dcPaletteOverride = enable;
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
