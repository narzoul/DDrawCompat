#include <Windows.h>

#include <Common/Hook.h>
#include <Common/Log.h>
#include <Win32/MemoryManagement.h>

namespace
{
	void limitTo2Gb(SIZE_T& mem);

	BOOL WINAPI getDiskFreeSpaceW(LPCWSTR lpRootPathName, LPDWORD lpSectorsPerCluster, LPDWORD lpBytesPerSector,
		LPDWORD lpNumberOfFreeClusters, LPDWORD lpTotalNumberOfClusters)
	{
		LOG_FUNC("GetDiskFreeSpaceW", lpRootPathName, lpSectorsPerCluster, lpBytesPerSector,
			lpNumberOfFreeClusters, lpTotalNumberOfClusters);

		DWORD sectorsPerCluster = 0;
		DWORD bytesPerSector = 0;
		DWORD numberOfFreeClusters = 0;
		DWORD totalNumberOfClusters = 0;

		if (!lpSectorsPerCluster)
		{
			lpSectorsPerCluster = &sectorsPerCluster;
		}
		if (!lpBytesPerSector)
		{
			lpBytesPerSector = &bytesPerSector;
		}
		if (!lpNumberOfFreeClusters)
		{
			lpNumberOfFreeClusters = &numberOfFreeClusters;
		}
		if (!lpTotalNumberOfClusters)
		{
			lpTotalNumberOfClusters = &totalNumberOfClusters;
		}

		auto result = CALL_ORIG_FUNC(GetDiskFreeSpaceW)(lpRootPathName, lpSectorsPerCluster, lpBytesPerSector,
			lpNumberOfFreeClusters, lpTotalNumberOfClusters);
		if (result)
		{
			const DWORD bytesPerCluster = *lpSectorsPerCluster * *lpBytesPerSector;
			if (0 != bytesPerCluster)
			{
				const DWORD maxClusters = INT_MAX / bytesPerCluster;
				*lpNumberOfFreeClusters = std::min(*lpNumberOfFreeClusters, maxClusters);
				*lpTotalNumberOfClusters = std::min(*lpTotalNumberOfClusters, maxClusters);
			}
		}
		return LOG_RESULT(result);
	}

	void WINAPI globalMemoryStatus(LPMEMORYSTATUS lpBuffer)
	{
		LOG_FUNC("GlobalMemoryStatus", lpBuffer);
		CALL_ORIG_FUNC(GlobalMemoryStatus)(lpBuffer);
		limitTo2Gb(lpBuffer->dwTotalPhys);
		limitTo2Gb(lpBuffer->dwAvailPhys);
		limitTo2Gb(lpBuffer->dwTotalPageFile);
		limitTo2Gb(lpBuffer->dwAvailPageFile);
		limitTo2Gb(lpBuffer->dwTotalVirtual);
		limitTo2Gb(lpBuffer->dwAvailVirtual);
	}

	void limitTo2Gb(SIZE_T& mem)
	{
		if (mem > INT_MAX)
		{
			mem = INT_MAX;
		}
	}
}

namespace Win32
{
	namespace MemoryManagement
	{
		void installHooks()
		{
			HOOK_FUNCTION(kernel32, GetDiskFreeSpaceW, getDiskFreeSpaceW);
			HOOK_FUNCTION(kernel32, GlobalMemoryStatus, globalMemoryStatus);
		}
	}
}
