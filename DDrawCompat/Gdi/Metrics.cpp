#include <Windows.h>

#include <Common/Hook.h>
#include <Common/Log.h>
#include <Gdi/Metrics.h>
#include <Win32/DisplayMode.h>

namespace
{
	decltype(&GetSystemMetricsForDpi) g_origGetSystemMetricsForDpi = nullptr;

	int getAdjustedDisplayMetrics(int nIndex, int cxIndex)
	{
		int result = CALL_ORIG_FUNC(GetSystemMetrics)(nIndex);
		auto dm = Win32::DisplayMode::getEmulatedDisplayMode();
		if (0 == dm.rect.left && 0 == dm.rect.top)
		{
			result += (nIndex == cxIndex) ? dm.diff.cx : dm.diff.cy;
		}
		return result;
	}

	int WINAPI getSystemMetrics(int nIndex)
	{
		LOG_FUNC("GetSystemMetrics", nIndex);

		switch (nIndex)
		{
		case SM_CXSCREEN:
		case SM_CYSCREEN:
		{
			return LOG_RESULT(getAdjustedDisplayMetrics(nIndex, SM_CXSCREEN));
		}

		case SM_CXFULLSCREEN:
		case SM_CYFULLSCREEN:
		{
			return LOG_RESULT(getAdjustedDisplayMetrics(nIndex, SM_CXFULLSCREEN));
		}

		case SM_CXMAXIMIZED:
		case SM_CYMAXIMIZED:
		{
			return LOG_RESULT(getAdjustedDisplayMetrics(nIndex, SM_CXMAXIMIZED));
		}

		case SM_CXSIZE:
			nIndex = SM_CYSIZE;
			break;
		}

		return LOG_RESULT(CALL_ORIG_FUNC(GetSystemMetrics)(nIndex));
	}

	int WINAPI getSystemMetricsForDpi(int nIndex, UINT dpi)
	{
		LOG_FUNC("GetSystemMetricsForDpi", nIndex, dpi);
		if (SM_CXSIZE == nIndex)
		{
			nIndex = SM_CYSIZE;
		}
		return LOG_RESULT(g_origGetSystemMetricsForDpi(nIndex, dpi));
	}
}

namespace Gdi
{
	namespace Metrics
	{
		void installHooks()
		{
			HOOK_FUNCTION(user32, GetSystemMetrics, getSystemMetrics);
			Compat::hookFunction("user32", "GetSystemMetricsForDpi",
				reinterpret_cast<void*&>(g_origGetSystemMetricsForDpi), getSystemMetricsForDpi);
		}
	}
}
