#include <atomic>
#include <string>

#include <Windows.h>
#include <VersionHelpers.h>

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
	Compat::SrwLock g_adapterInfoSrwLock;
	std::string g_lastDDrawDeviceName;

	long long g_qpcLastVsync = 0;
	UINT g_vsyncCounter = 0;
	CONDITION_VARIABLE g_vsyncCounterCv = CONDITION_VARIABLE_INIT;
	Compat::SrwLock g_vsyncCounterSrwLock;

	void getVidPnSource(D3DKMT_HANDLE& adapter, UINT& vidPnSourceId);
	void updateGdiAdapterInfo();
	void waitForVerticalBlank();

	NTSTATUS APIENTRY closeAdapter(const D3DKMT_CLOSEADAPTER* pData)
	{
		Compat::ScopedSrwLockExclusive lock(g_adapterInfoSrwLock);
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

	int getScanLine()
	{
		D3DKMT_GETSCANLINE data = {};
		getVidPnSource(data.hAdapter, data.VidPnSourceId);
		if (!data.hAdapter || FAILED(D3DKMTGetScanLine(&data)) || data.InVerticalBlank)
		{
			return -1;
		}
		return data.ScanLine;
	}

	void getVidPnSource(D3DKMT_HANDLE& adapter, UINT& vidPnSourceId)
	{
		{
			Compat::ScopedSrwLockShared lock(g_adapterInfoSrwLock);
			if (g_lastOpenAdapterInfo.adapter)
			{
				adapter = g_lastOpenAdapterInfo.adapter;
				vidPnSourceId = g_lastOpenAdapterInfo.vidPnSourceId;
				return;
			}
		}

		Compat::ScopedSrwLockExclusive lock(g_adapterInfoSrwLock);
		updateGdiAdapterInfo();
		adapter = g_gdiAdapterInfo.adapter;
		vidPnSourceId = g_gdiAdapterInfo.vidPnSourceId;
	}

	NTSTATUS APIENTRY openAdapterFromHdc(D3DKMT_OPENADAPTERFROMHDC* pData)
	{
		LOG_FUNC("D3DKMTOpenAdapterFromHdc", pData);
		NTSTATUS result = D3DKMTOpenAdapterFromHdc(pData);
		if (SUCCEEDED(result))
		{
			Compat::ScopedSrwLockExclusive lock(g_adapterInfoSrwLock);
			g_lastOpenAdapterInfo = getAdapterInfo(g_lastDDrawDeviceName, *pData);
		}
		return LOG_RESULT(result);
	}

	void pollForVerticalBlank()
	{
		int scanLine = getScanLine();
		int prevScanLine = scanLine;
		auto qpcStart = Time::queryPerformanceCounter();
		while (scanLine >= prevScanLine && Time::queryPerformanceCounter() - qpcStart < Time::g_qpcFrequency / 60)
		{
			Sleep(1);
			prevScanLine = scanLine;
			scanLine = getScanLine();
		}
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
				info->DedicatedVideoMemorySize += info->DedicatedSystemMemorySize;
				info->DedicatedSystemMemorySize = 0;

				const ULONGLONG maxMem = 0x3FFF0000;
				if (info->DedicatedVideoMemorySize < maxMem)
				{
					auto addedMem = min(maxMem - info->DedicatedVideoMemorySize, info->SharedSystemMemorySize);
					info->DedicatedVideoMemorySize += addedMem;
					info->SharedSystemMemorySize -= addedMem;
				}

				info->DedicatedVideoMemorySize = min(info->DedicatedVideoMemorySize, maxMem);
				info->SharedSystemMemorySize = min(info->SharedSystemMemorySize, maxMem);
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

			{
				Compat::ScopedSrwLockExclusive lock(g_vsyncCounterSrwLock);
				g_qpcLastVsync = Time::queryPerformanceCounter();
				++g_vsyncCounter;
			}

			WakeAllConditionVariable(&g_vsyncCounterCv);
		}
		return 0;
	}

	void waitForVerticalBlank()
	{
		if (IsWindows8OrGreater())
		{
			D3DKMT_WAITFORVERTICALBLANKEVENT data = {};

			{
				Compat::ScopedSrwLockShared lock(g_adapterInfoSrwLock);
				data.hAdapter = g_lastOpenAdapterInfo.adapter;
				data.VidPnSourceId = g_lastOpenAdapterInfo.vidPnSourceId;
			}

			if (!data.hAdapter)
			{
				updateGdiAdapterInfo();
				data.hAdapter = g_gdiAdapterInfo.adapter;
				data.VidPnSourceId = g_gdiAdapterInfo.vidPnSourceId;
			}

			if (data.hAdapter && SUCCEEDED(D3DKMTWaitForVerticalBlankEvent(&data)))
			{
				return;
			}
		}

		pollForVerticalBlank();
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
			Compat::ScopedSrwLockShared srwLock(g_adapterInfoSrwLock);
			return g_lastOpenAdapterInfo;
		}

		long long getQpcLastVsync()
		{
			Compat::ScopedSrwLockShared lock(g_vsyncCounterSrwLock);
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
