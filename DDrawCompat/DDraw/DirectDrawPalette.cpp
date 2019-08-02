#include <cstring>
#include <deque>

#include "Common/Time.h"
#include "Config/Config.h"
#include "DDraw/DirectDrawPalette.h"
#include "DDraw/Surfaces/PrimarySurface.h"
#include "Gdi/AccessGuard.h"

namespace DDraw
{
	void DirectDrawPalette::setCompatVtable(IDirectDrawPaletteVtbl& vtable)
	{
		vtable.SetEntries = &SetEntries;
	}

	HRESULT STDMETHODCALLTYPE DirectDrawPalette::SetEntries(
		IDirectDrawPalette* This,
		DWORD dwFlags,
		DWORD dwStartingEntry,
		DWORD dwCount,
		LPPALETTEENTRY lpEntries)
	{
		if (This == PrimarySurface::s_palette)
		{
			waitForNextUpdate();
		}

		HRESULT result = s_origVtable.SetEntries(This, dwFlags, dwStartingEntry, dwCount, lpEntries);
		if (SUCCEEDED(result) && This == PrimarySurface::s_palette)
		{
			PrimarySurface::updatePalette();
		}
		return result;
	}

	void DirectDrawPalette::waitForNextUpdate()
	{
		static std::deque<long long> updatesInLastMs;

		const long long qpcNow = Time::queryPerformanceCounter();
		const long long qpcLastMsBegin = qpcNow - Time::g_qpcFrequency / 1000;
		while (!updatesInLastMs.empty() && qpcLastMsBegin - updatesInLastMs.front() > 0)
		{
			updatesInLastMs.pop_front();
		}

		if (updatesInLastMs.size() >= Config::maxPaletteUpdatesPerMs)
		{
			Sleep(1);
			updatesInLastMs.clear();
		}

		updatesInLastMs.push_back(Time::queryPerformanceCounter());
	}
}
