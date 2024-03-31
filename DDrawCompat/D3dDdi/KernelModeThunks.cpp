#include <atomic>
#include <filesystem>
#include <string>

#include <Windows.h>
#include <VersionHelpers.h>

#include <Common/Log.h>
#include <Common/Hook.h>
#include <Common/ScopedSrwLock.h>
#include <Common/Time.h>
#include <Config/Settings/ForceD3D9On12.h>
#include <Config/Settings/FullscreenMode.h>
#include <D3dDdi/Device.h>
#include <D3dDdi/KernelModeThunks.h>
#include <D3dDdi/Log/KernelModeThunksLog.h>
#include <D3dDdi/Resource.h>
#include <D3dDdi/ScopedCriticalSection.h>
#include <DDraw/RealPrimarySurface.h>
#include <DDraw/ScopedThreadLock.h>
#include <DDraw/Surfaces/PrimarySurface.h>
#include <Gdi/Palette.h>
#include <Gdi/Window.h>
#include <Win32/DisplayMode.h>
#include <Win32/DpiAwareness.h>

namespace
{
	D3DDDIFORMAT g_dcFormatOverride = D3DDDIFMT_UNKNOWN;
	PALETTEENTRY* g_dcPaletteOverride = nullptr;
	D3dDdi::KernelModeThunks::AdapterInfo g_gdiAdapterInfo = {};
	D3dDdi::KernelModeThunks::AdapterInfo g_lastOpenAdapterInfo = {};
	Compat::SrwLock g_adapterInfoSrwLock;
	std::string g_lastDDrawDeviceName;
	bool g_isExclusiveFullscreen = false;
	decltype(&D3DKMTSubmitPresentBltToHwQueue) g_origSubmitPresentBltToHwQueue = nullptr;
	decltype(&D3DKMTSubmitPresentToHwQueue) g_origSubmitPresentToHwQueue = nullptr;

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

	NTSTATUS APIENTRY createDevice(D3DKMT_CREATEDEVICE* pData)
	{
		LOG_FUNC("D3DKMTCreateDevice", pData);
		NTSTATUS result = D3DKMTCreateDevice(pData);
		if (SUCCEEDED(result))
		{
			D3DKMT_SETQUEUEDLIMIT limit = {};
			limit.hDevice = pData->hDevice;
			limit.Type = D3DKMT_SET_QUEUEDLIMIT_PRESENT;
			limit.QueuedPresentLimit = 1;
			D3DKMTSetQueuedLimit(&limit);
		}
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

	D3dDdi::KernelModeThunks::AdapterInfo getAdapterInfo(const std::string& deviceName, const D3DKMT_OPENADAPTERFROMHDC& data)
	{
		D3dDdi::KernelModeThunks::AdapterInfo adapterInfo = {};
		adapterInfo.adapter = data.hAdapter;
		adapterInfo.vidPnSourceId = data.VidPnSourceId;
		adapterInfo.luid = data.AdapterLuid;
		adapterInfo.deviceName = std::wstring(deviceName.begin(), deviceName.end());
		return adapterInfo;
	}

	int getScanLine()
	{
		D3DKMT_GETSCANLINE data = {};
		getVidPnSource(data.hAdapter, data.VidPnSourceId);
		if (!data.hAdapter || FAILED(D3DKMTGetScanLine(&data)))
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

	NTSTATUS APIENTRY present(D3DKMT_PRESENT* pData)
	{
		LOG_FUNC("D3DKMTPresent", pData);
		D3dDdi::KernelModeThunks::fixPresent(*pData);
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
				if (Config::forceD3D9On12.get() &&
					KMTUMDVERSION_DX9 == static_cast<D3DKMT_UMDFILENAMEINFO*>(pData->pPrivateDriverData)->Version)
				{
					return STATUS_INVALID_PARAMETER;
				}
				break;

			case KMTQAITYPE_GETSEGMENTSIZE:
			{
				auto info = static_cast<D3DKMT_SEGMENTSIZEINFO*>(pData->pPrivateDriverData);
				info->DedicatedVideoMemorySize += info->DedicatedSystemMemorySize;
				info->DedicatedSystemMemorySize = 0;

				const ULONGLONG maxMem = 0x3FFF0000;
				if (info->DedicatedVideoMemorySize < maxMem)
				{
					auto addedMem = std::min(maxMem - info->DedicatedVideoMemorySize, info->SharedSystemMemorySize);
					info->DedicatedVideoMemorySize += addedMem;
					info->SharedSystemMemorySize -= addedMem;
				}

				info->DedicatedVideoMemorySize = std::min(info->DedicatedVideoMemorySize, maxMem);
				info->SharedSystemMemorySize = std::min(info->SharedSystemMemorySize, maxMem);
				break;
			}
			}
		}
		else if (!Config::forceD3D9On12.get() &&
			KMTQAITYPE_UMDRIVERNAME == pData->Type &&
			KMTUMDVERSION_DX9 == static_cast<D3DKMT_UMDFILENAMEINFO*>(pData->pPrivateDriverData)->Version)
		{
			D3DKMT_UMDFILENAMEINFO fn = {};
			fn.Version = KMTUMDVERSION_DX12;
			D3DKMT_QUERYADAPTERINFO data = *pData;
			data.pPrivateDriverData = &fn;
			data.PrivateDriverDataSize = sizeof(fn);
			if (SUCCEEDED(D3DKMTQueryAdapterInfo(&data)))
			{
				std::filesystem::path path(fn.UmdFileName);
				path.replace_filename("igd9trinity32.dll");
				HMODULE mod = LoadLibraryExW(path.native().c_str(), nullptr, LOAD_LIBRARY_AS_DATAFILE);
				if (mod)
				{
					FreeLibrary(mod);
					memcpy(static_cast<D3DKMT_UMDFILENAMEINFO*>(pData->pPrivateDriverData)->UmdFileName,
						path.wstring().c_str(), (path.native().length() + 1) * 2);
					LOG_ONCE("Replacing D3D9On12 with igd9trinity32.dll");
					return LOG_RESULT(S_OK);
				}
			}
		}
		return LOG_RESULT(result);
	}

	NTSTATUS APIENTRY releaseProcessVidPnSourceOwners(HANDLE hProcess)
	{
		LOG_FUNC("D3DKMTReleaseProcessVidPnSourceOwners", hProcess);
		NTSTATUS result = D3DKMTReleaseProcessVidPnSourceOwners(hProcess);
		if (SUCCEEDED(result))
		{
			g_isExclusiveFullscreen = false;
		}
		return LOG_RESULT(result);
	}

	NTSTATUS APIENTRY setGammaRamp(const D3DKMT_SETGAMMARAMP* pData)
	{
		LOG_FUNC("D3DKMTSetGammaRamp", pData);
		NTSTATUS result = 0;
		UINT vsyncCounter = D3dDdi::KernelModeThunks::getVsyncCounter();
		if (g_isExclusiveFullscreen || D3DDDI_GAMMARAMP_RGB256x3x16 != pData->Type || !pData->pGammaRampRgb256x3x16)
		{
			D3dDdi::ShaderBlitter::resetGammaRamp();
			result = D3DKMTSetGammaRamp(pData);
		}
		else
		{
			D3dDdi::ShaderBlitter::setGammaRamp(*pData->pGammaRampRgb256x3x16);
			DDraw::RealPrimarySurface::scheduleUpdate();
		}
		if (SUCCEEDED(result))
		{
			D3dDdi::KernelModeThunks::waitForVsyncCounter(vsyncCounter + 1);
		}
		return LOG_RESULT(result);
	}

	NTSTATUS APIENTRY setVidPnSourceOwner(const D3DKMT_SETVIDPNSOURCEOWNER* pData)
	{
		LOG_FUNC("D3DKMTSetVidPnSourceOwner", pData);
		NTSTATUS result = D3DKMTSetVidPnSourceOwner(pData);
		if (SUCCEEDED(result))
		{
			g_isExclusiveFullscreen = 0 != pData->VidPnSourceCount;
		}
		return LOG_RESULT(result);
	}

	NTSTATUS APIENTRY submitPresentToHwQueue(D3DKMT_SUBMITPRESENTTOHWQUEUE* pData)
	{
		LOG_FUNC("D3DKMTSubmitPresentToHwQueue", pData);
		D3dDdi::KernelModeThunks::fixPresent(pData->PrivatePresentData);
		return LOG_RESULT(g_origSubmitPresentToHwQueue(pData));
	}

	NTSTATUS APIENTRY submitPresentBltToHwQueue(const D3DKMT_SUBMITPRESENTBLTTOHWQUEUE* pData)
	{
		LOG_FUNC("D3DKMTSubmitPresentBltToHwQueue", pData);
		D3dDdi::KernelModeThunks::fixPresent(const_cast<D3DKMT_PRESENT&>(pData->PrivatePresentData));
		return LOG_RESULT(g_origSubmitPresentBltToHwQueue(pData));
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
			CALL_ORIG_FUNC(GetMonitorInfoA)(MonitorFromPoint({}, MONITOR_DEFAULTTOPRIMARY), &mi);

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
		auto qpcStart = Time::queryPerformanceCounter();
		int scanLine = getScanLine();
		int prevScanLine = 0;
		while (scanLine >= prevScanLine)
		{
			Time::waitForNextTick();
			prevScanLine = scanLine;
			scanLine = getScanLine();
		}

		if (scanLine < 0)
		{
			auto msElapsed = static_cast<DWORD>(Time::qpcToMs(Time::queryPerformanceCounter() - qpcStart));
			if (msElapsed < 16)
			{
				Sleep(16 - msElapsed);
			}
		}
	}
}

namespace D3dDdi
{
	namespace KernelModeThunks
	{
		void fixPresent(D3DKMT_PRESENT& data)
		{
			static RECT rect = {};
			HWND presentationWindow = DDraw::RealPrimarySurface::getPresentationWindow();
			if (presentationWindow)
			{
				Win32::ScopedDpiAwareness dpiAwareness;
				GetWindowRect(presentationWindow, &rect);
				OffsetRect(&rect, -rect.left, -rect.top);
				data.SrcRect = rect;
				data.DstRect = rect;
				if (1 == data.SubRectCnt)
				{
					data.pSrcSubRects = &rect;
				}
			}
		}

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
			Compat::hookIatFunction(Dll::g_origDDrawModule, "D3DKMTCreateDevice", createDevice);
			Compat::hookIatFunction(Dll::g_origDDrawModule, "D3DKMTOpenAdapterFromHdc", openAdapterFromHdc);
			Compat::hookIatFunction(Dll::g_origDDrawModule, "D3DKMTPresent", present);
			Compat::hookIatFunction(Dll::g_origDDrawModule, "D3DKMTQueryAdapterInfo", queryAdapterInfo);
			Compat::hookIatFunction(Dll::g_origDDrawModule, "D3DKMTReleaseProcessVidPnSourceOwners", releaseProcessVidPnSourceOwners);
			Compat::hookIatFunction(Dll::g_origDDrawModule, "D3DKMTSetGammaRamp", setGammaRamp);
			Compat::hookIatFunction(Dll::g_origDDrawModule, "D3DKMTSetVidPnSourceOwner", setVidPnSourceOwner);

			auto gdi32 = GetModuleHandle("gdi32");
			g_origSubmitPresentBltToHwQueue = reinterpret_cast<decltype(&D3DKMTSubmitPresentBltToHwQueue)>(
				GetProcAddress(gdi32, "D3DKMTSubmitPresentBltToHwQueue"));
			if (g_origSubmitPresentBltToHwQueue)
			{
				Compat::hookIatFunction(Dll::g_origDDrawModule, "D3DKMTSubmitPresentBltToHwQueue", submitPresentBltToHwQueue);
			}

			g_origSubmitPresentToHwQueue = reinterpret_cast<decltype(&D3DKMTSubmitPresentToHwQueue)>(
				GetProcAddress(gdi32, "D3DKMTSubmitPresentToHwQueue"));
			if (g_origSubmitPresentToHwQueue)
			{
				Compat::hookIatFunction(Dll::g_origDDrawModule, "D3DKMTSubmitPresentToHwQueue", submitPresentToHwQueue);
			}

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
