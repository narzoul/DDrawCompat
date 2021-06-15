#include <atomic>
#include <string>

#include <Common/Log.h>
#include <Common/Hook.h>
#include <Common/ScopedSrwLock.h>
#include <Common/Time.h>
#include <D3dDdi/Device.h>
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
	D3DDDIFORMAT g_dcFormatOverride = D3DDDIFMT_UNKNOWN;
	PALETTEENTRY* g_dcPaletteOverride = nullptr;
	D3dDdi::KernelModeThunks::AdapterInfo g_gdiAdapterInfo = {};
	D3dDdi::KernelModeThunks::AdapterInfo g_lastOpenAdapterInfo = {};
	Compat::SrwLock g_lastOpenAdapterInfoSrwLock;
	std::string g_lastDDrawDeviceName;

	std::atomic<long long> g_qpcLastVsync = 0;
	UINT g_vsyncCounter = 0;
	CONDITION_VARIABLE g_vsyncCounterCv = CONDITION_VARIABLE_INIT;
	Compat::SrwLock g_vsyncCounterSrwLock;

	void waitForVerticalBlank();

	NTSTATUS APIENTRY closeAdapter(const D3DKMT_CLOSEADAPTER* pData)
	{
		Compat::ScopedSrwLockExclusive lock(g_lastOpenAdapterInfoSrwLock);
		if (pData && pData->hAdapter == g_lastOpenAdapterInfo.adapter)
		{
			g_lastOpenAdapterInfo = {};
		}
		return D3DKMTCloseAdapter(pData);
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
				pData->pColorTable = g_dcPaletteOverride;
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
		if (pwszDevice)
		{
			g_lastDDrawDeviceName = pwszDevice;
		}
		else
		{
			MONITORINFOEXA mi = {};
			mi.cbSize = sizeof(mi);
			CALL_ORIG_FUNC(GetMonitorInfoA)(MonitorFromPoint({}, MONITOR_DEFAULTTOPRIMARY), &mi);
			g_lastDDrawDeviceName = mi.szDevice;
		}
		return LOG_RESULT(CreateDCA(pwszDriver, pwszDevice, pszPort, pdm));
	}

	BOOL CALLBACK findMonitorInfo(HMONITOR hMonitor, HDC /*hdcMonitor*/, LPRECT /*lprcMonitor*/, LPARAM dwData)
	{
		MONITORINFOEXW mi = {};
		mi.cbSize = sizeof(mi);
		CALL_ORIG_FUNC(GetMonitorInfoW)(hMonitor, &mi);
		if (0 == wcscmp(reinterpret_cast<MONITORINFOEXW*>(dwData)->szDevice, mi.szDevice))
		{
			*reinterpret_cast<MONITORINFOEXW*>(dwData) = mi;
			return FALSE;
		}
		return TRUE;
	}

	D3dDdi::KernelModeThunks::AdapterInfo getAdapterInfo(const std::string& deviceName, const D3DKMT_OPENADAPTERFROMHDC& data)
	{
		D3dDdi::KernelModeThunks::AdapterInfo adapterInfo = {};
		adapterInfo.adapter = data.hAdapter;
		adapterInfo.vidPnSourceId = data.VidPnSourceId;
		adapterInfo.luid = data.AdapterLuid;
		wcscpy_s(adapterInfo.monitorInfo.szDevice, std::wstring(deviceName.begin(), deviceName.end()).c_str());
		EnumDisplayMonitors(nullptr, nullptr, findMonitorInfo, reinterpret_cast<LPARAM>(&adapterInfo.monitorInfo));
		return adapterInfo;
	}

	NTSTATUS APIENTRY openAdapterFromHdc(D3DKMT_OPENADAPTERFROMHDC* pData)
	{
		LOG_FUNC("D3DKMTOpenAdapterFromHdc", pData);
		NTSTATUS result = D3DKMTOpenAdapterFromHdc(pData);
		if (SUCCEEDED(result))
		{
			Compat::ScopedSrwLockExclusive lock(g_lastOpenAdapterInfoSrwLock);
			g_lastOpenAdapterInfo = getAdapterInfo(g_lastDDrawDeviceName, *pData);
		}
		return LOG_RESULT(result);
	}

	NTSTATUS APIENTRY queryAdapterInfo(const D3DKMT_QUERYADAPTERINFO* pData)
	{
		LOG_FUNC("D3DKMTQueryAdapterInfo", pData);
		NTSTATUS result = D3DKMTQueryAdapterInfo(pData);
		if (SUCCEEDED(result))
		{
			switch (pData->Type)
			{
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
		UINT vsyncCounter = D3dDdi::KernelModeThunks::getVsyncCounter();
		DDraw::RealPrimarySurface::flush();
		HRESULT result = D3DKMTSetGammaRamp(pData);
		if (SUCCEEDED(result))
		{
			D3dDdi::KernelModeThunks::waitForVsyncCounter(vsyncCounter + 1);
		}
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
				g_gdiAdapterInfo = getAdapterInfo(mi.szDevice, data);
			}
			DeleteDC(data.hDc);

			lastDisplaySettingsUniqueness = currentDisplaySettingsUniqueness;
		}
	}

	unsigned WINAPI vsyncThreadProc(LPVOID /*lpParameter*/)
	{
		while (true)
		{
			waitForVerticalBlank();
			g_qpcLastVsync = Time::queryPerformanceCounter();

			{
				Compat::ScopedSrwLockExclusive lock(g_vsyncCounterSrwLock);
				++g_vsyncCounter;
			}

			WakeAllConditionVariable(&g_vsyncCounterCv);
		}
		return 0;
	}

	void waitForVerticalBlank()
	{
		D3DKMT_WAITFORVERTICALBLANKEVENT data = {};

		{
			Compat::ScopedSrwLockShared lock(g_lastOpenAdapterInfoSrwLock);
			data.hAdapter = g_lastOpenAdapterInfo.adapter;
			data.VidPnSourceId = g_lastOpenAdapterInfo.vidPnSourceId;
		}

		if (!data.hAdapter)
		{
			updateGdiAdapterInfo();
			data.hAdapter = g_gdiAdapterInfo.adapter;
			data.VidPnSourceId = g_gdiAdapterInfo.vidPnSourceId;
		}

		if (!data.hAdapter || FAILED(D3DKMTWaitForVerticalBlankEvent(&data)))
		{
			Sleep(16);
		}
	}
}

namespace D3dDdi
{
	namespace KernelModeThunks
	{
		AdapterInfo getAdapterInfo(CompatRef<IDirectDraw7> dd)
		{
			DDraw::ScopedThreadLock lock;
			DDDEVICEIDENTIFIER2 di = {};
			dd.get().lpVtbl->GetDeviceIdentifier(&dd, &di, 0);
			return getLastOpenAdapterInfo();
		}

		AdapterInfo getLastOpenAdapterInfo()
		{
			Compat::ScopedSrwLockShared srwLock(g_lastOpenAdapterInfoSrwLock);
			return g_lastOpenAdapterInfo;
		}

		long long getQpcLastVsync()
		{
			return g_qpcLastVsync;
		}

		UINT getVsyncCounter()
		{
			Compat::ScopedSrwLockShared lock(g_vsyncCounterSrwLock);
			return g_vsyncCounter;
		}

		void installHooks()
		{
			Compat::hookIatFunction(Dll::g_origDDrawModule, "CreateDCA", ddrawCreateDcA);
			Compat::hookIatFunction(Dll::g_origDDrawModule, "D3DKMTCloseAdapter", closeAdapter);
			Compat::hookIatFunction(Dll::g_origDDrawModule, "D3DKMTCreateDCFromMemory", createDcFromMemory);
			Compat::hookIatFunction(Dll::g_origDDrawModule, "D3DKMTOpenAdapterFromHdc", openAdapterFromHdc);
			Compat::hookIatFunction(Dll::g_origDDrawModule, "D3DKMTQueryAdapterInfo", queryAdapterInfo);
			Compat::hookIatFunction(Dll::g_origDDrawModule, "D3DKMTSetGammaRamp", setGammaRamp);

			Dll::createThread(&vsyncThreadProc, nullptr, THREAD_PRIORITY_TIME_CRITICAL);
		}

		void setDcFormatOverride(UINT format)
		{
			g_dcFormatOverride = static_cast<D3DDDIFORMAT>(format);
		}

		void setDcPaletteOverride(PALETTEENTRY* palette)
		{
			g_dcPaletteOverride = palette;
		}

		void waitForVsync()
		{
			waitForVsyncCounter(getVsyncCounter() + 1);
		}

		bool waitForVsyncCounter(UINT counter)
		{
			bool waited = false;
			Compat::ScopedSrwLockShared lock(g_vsyncCounterSrwLock);
			while (static_cast<INT>(g_vsyncCounter - counter) < 0)
			{
				SleepConditionVariableSRW(&g_vsyncCounterCv, &g_vsyncCounterSrwLock, INFINITE,
					CONDITION_VARIABLE_LOCKMODE_SHARED);
				waited = true;
			}
			return waited;
		}
	}
}
