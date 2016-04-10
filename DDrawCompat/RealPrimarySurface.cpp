#include <atomic>

#include "CompatDirectDraw.h"
#include "CompatDirectDrawPalette.h"
#include "CompatDirectDrawSurface.h"
#include "CompatGdi.h"
#include "CompatPaletteConverter.h"
#include "CompatPrimarySurface.h"
#include "Config.h"
#include "DDrawProcs.h"
#include "DDrawTypes.h"
#include "Hook.h"
#include "IReleaseNotifier.h"
#include "RealPrimarySurface.h"

namespace
{
	void onRelease();
	void updateNow();

	IDirectDrawSurface7* g_frontBuffer = nullptr;
	IDirectDrawSurface7* g_backBuffer = nullptr;
	IReleaseNotifier g_releaseNotifier(onRelease);

	HANDLE g_updateThread = nullptr;
	HANDLE g_updateEvent = nullptr;
	RECT g_updateRect = {};
	bool g_isFlipEvent = false;
	LARGE_INTEGER g_lastUpdateTime = {};
	LARGE_INTEGER g_qpcFrequency = {};

	std::atomic<bool> g_isFullScreen(false);

	bool compatBlt(IDirectDrawSurface7* dest)
	{
		Compat::LogEnter("RealPrimarySurface::compatBlt", dest);

		bool result = false;
		const auto& origVtable = CompatDirectDrawSurface<IDirectDrawSurface7>::s_origVtable;

		if (!RealPrimarySurface::isFullScreen())
		{
			IDirectDrawClipper* clipper = nullptr;
			if (FAILED(origVtable.GetClipper(g_frontBuffer, &clipper)))
			{
				return false;
			}
			clipper->lpVtbl->Release(clipper);
		}

		if (CompatPrimarySurface::pixelFormat.dwRGBBitCount <= 8)
		{
			IDirectDrawSurface7* converterSurface = CompatPaletteConverter::lockSurface();
			HDC converterDc = CompatPaletteConverter::lockDc();

			origVtable.Blt(converterSurface, &g_updateRect,
				CompatPrimarySurface::surface, &g_updateRect, DDBLT_WAIT, nullptr);

			HDC destDc = nullptr;
			origVtable.GetDC(dest, &destDc);
			result = TRUE == CALL_ORIG_FUNC(BitBlt)(destDc, g_updateRect.left, g_updateRect.top,
				g_updateRect.right - g_updateRect.left, g_updateRect.bottom - g_updateRect.top,
				converterDc, g_updateRect.left, g_updateRect.top, SRCCOPY);
			origVtable.ReleaseDC(dest, destDc);

			CompatPaletteConverter::unlockDc();
			CompatPaletteConverter::unlockSurface();

			if (dest == g_frontBuffer)
			{
				// Force the screen to be updated. It won't refresh from BitBlt alone.
				RECT r = { 0, 0, 1, 1 };
				CompatDirectDrawSurface<IDirectDrawSurface7>::s_origVtable.BltFast(
					g_frontBuffer, 0, 0, g_frontBuffer, &r, DDBLTFAST_WAIT);
			}
		}
		else
		{
			result = SUCCEEDED(origVtable.Blt(dest, &g_updateRect,
				CompatPrimarySurface::surface, &g_updateRect, DDBLT_WAIT, nullptr));
		}

		if (result)
		{
			SetRectEmpty(&g_updateRect);
		}

		Compat::LogLeave("RealPrimarySurface::compatBlt", dest);
		return result;
	}
	
	DWORD getTimeElapsedInMs(const LARGE_INTEGER& time)
	{
		LARGE_INTEGER currentTime = {};
		QueryPerformanceCounter(&currentTime);
		return static_cast<DWORD>((currentTime.QuadPart - time.QuadPart) * 1000 / g_qpcFrequency.QuadPart);
	}

	void onRelease()
	{
		Compat::LogEnter("RealPrimarySurface::onRelease");

		g_frontBuffer = nullptr;
		g_backBuffer = nullptr;
		g_isFullScreen = false;
		CompatPaletteConverter::release();

		ZeroMemory(&RealPrimarySurface::s_surfaceDesc, sizeof(RealPrimarySurface::s_surfaceDesc));

		Compat::LogLeave("RealPrimarySurface::onRelease");
	}

	void updateNow()
	{
		QueryPerformanceCounter(&g_lastUpdateTime);
		compatBlt(g_frontBuffer);
	}

	DWORD WINAPI updateThreadProc(LPVOID /*lpParameter*/)
	{
		while (true)
		{
			WaitForSingleObject(g_updateEvent, INFINITE);

			Compat::origProcs.AcquireDDThreadLock();
			ResetEvent(g_updateEvent);
			if (!g_frontBuffer)
			{
				Compat::origProcs.ReleaseDDThreadLock();
				continue;
			}

			if (!g_isFlipEvent)
			{
				updateNow();
			}

			DWORD timeSinceLastUpdate = getTimeElapsedInMs(g_lastUpdateTime);
			DWORD minRefreshInterval = g_isFlipEvent ?
				Config::minRefreshIntervalAfterFlip : Config::minRefreshInterval;
			DWORD minRefreshIntervalTimeout = timeSinceLastUpdate < minRefreshInterval ?
				minRefreshInterval - timeSinceLastUpdate : 0;

			g_isFlipEvent = false;
			Compat::origProcs.ReleaseDDThreadLock();

			if (minRefreshIntervalTimeout)
			{
				Sleep(minRefreshIntervalTimeout);
			}
		}
	}
}

DDSURFACEDESC2 RealPrimarySurface::s_surfaceDesc = {};

template <typename DirectDraw>
HRESULT RealPrimarySurface::create(DirectDraw& dd)
{
	typename Types<DirectDraw>::TSurfaceDesc desc = {};
	desc.dwSize = sizeof(desc);
	desc.dwFlags = DDSD_CAPS | DDSD_BACKBUFFERCOUNT;
	desc.ddsCaps.dwCaps = DDSCAPS_PRIMARYSURFACE | DDSCAPS_COMPLEX | DDSCAPS_FLIP;
	desc.dwBackBufferCount = 1;

	typename Types<DirectDraw>::TCreatedSurface* surface = nullptr;
	HRESULT result = CompatDirectDraw<DirectDraw>::s_origVtable.CreateSurface(
		&dd, &desc, &surface, nullptr);

	bool isFlippable = true;
	if (DDERR_NOEXCLUSIVEMODE == result)
	{
		desc.dwFlags = DDSD_CAPS;
		desc.ddsCaps.dwCaps = DDSCAPS_PRIMARYSURFACE;
		desc.dwBackBufferCount = 0;
		isFlippable = false;
		result = CompatDirectDraw<DirectDraw>::s_origVtable.CreateSurface(
			&dd, &desc, &surface, nullptr);
	}

	if (FAILED(result))
	{
		Compat::Log() << "Failed to create the real primary surface";
		return result;
	}

	surface->lpVtbl->QueryInterface(
		surface, IID_IDirectDrawSurface7, reinterpret_cast<LPVOID*>(&g_frontBuffer));
	surface->lpVtbl->Release(surface);

	s_surfaceDesc.dwSize = sizeof(s_surfaceDesc);
	g_frontBuffer->lpVtbl->GetSurfaceDesc(g_frontBuffer, &s_surfaceDesc);

	if (!CompatPaletteConverter::create())
	{
		g_frontBuffer->lpVtbl->Release(g_frontBuffer);
		g_frontBuffer = nullptr;
		return DDERR_GENERIC;
	}

	if (isFlippable)
	{
		DDSCAPS2 backBufferCaps = {};
		backBufferCaps.dwCaps = DDSCAPS_BACKBUFFER;
		g_frontBuffer->lpVtbl->GetAttachedSurface(g_frontBuffer, &backBufferCaps, &g_backBuffer);
	}

	QueryPerformanceFrequency(&g_qpcFrequency);

	if (!g_updateEvent)
	{
		g_updateEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);
	}

	if (!g_updateThread)
	{
		g_updateThread = CreateThread(nullptr, 0, &updateThreadProc, nullptr, 0, nullptr);
		SetThreadPriority(g_updateThread, THREAD_PRIORITY_ABOVE_NORMAL);
	}

	g_frontBuffer->lpVtbl->SetPrivateData(g_frontBuffer,
		IID_IReleaseNotifier, &g_releaseNotifier, sizeof(&g_releaseNotifier), DDSPD_IUNKNOWNPOINTER);

	g_isFullScreen = isFlippable;

	return DD_OK;
}

template HRESULT RealPrimarySurface::create(IDirectDraw&);
template HRESULT RealPrimarySurface::create(IDirectDraw2&);
template HRESULT RealPrimarySurface::create(IDirectDraw4&);
template HRESULT RealPrimarySurface::create(IDirectDraw7&);

HRESULT RealPrimarySurface::flip(DWORD flags)
{
	if (!g_backBuffer)
	{
		return DDERR_NOTFLIPPABLE;
	}

	invalidate(nullptr);
	compatBlt(g_backBuffer);
	if (flags & DDFLIP_DONOTWAIT)
	{
		flags ^= DDFLIP_DONOTWAIT;
	}
	
	HRESULT result = g_frontBuffer->lpVtbl->Flip(g_frontBuffer, nullptr, flags | DDFLIP_WAIT);
	if (SUCCEEDED(result))
	{
		QueryPerformanceCounter(&g_lastUpdateTime);
		g_isFlipEvent = true;
		SetEvent(g_updateEvent);
	}
	return result;
}

IDirectDrawSurface7* RealPrimarySurface::getSurface()
{
	return g_frontBuffer;
}

void RealPrimarySurface::invalidate(const RECT* rect)
{
	if (rect)
	{
		UnionRect(&g_updateRect, &g_updateRect, rect);
	}
	else
	{
		SetRect(&g_updateRect, 0, 0,
			CompatPrimarySurface::displayMode.width, CompatPrimarySurface::displayMode.height);
	}
}

bool RealPrimarySurface::isFullScreen()
{
	return g_isFullScreen;
}

bool RealPrimarySurface::isLost()
{
	return g_frontBuffer &&
		DDERR_SURFACELOST == CompatDirectDrawSurface<IDirectDrawSurface7>::s_origVtable.IsLost(g_frontBuffer);
}

void RealPrimarySurface::release()
{
	if (g_frontBuffer)
	{
		g_frontBuffer->lpVtbl->Release(g_frontBuffer);
	}
}

HRESULT RealPrimarySurface::restore()
{
	return g_frontBuffer->lpVtbl->Restore(g_frontBuffer);
}

void RealPrimarySurface::setClipper(LPDIRECTDRAWCLIPPER clipper)
{
	CompatPaletteConverter::setClipper(clipper);

	HRESULT result = CompatDirectDrawSurface<IDirectDrawSurface7>::s_origVtable.SetClipper(
		g_frontBuffer, clipper);
	if (FAILED(result))
	{
		LOG_ONCE("Failed to set clipper on the real primary surface: " << result);
	}
}

void RealPrimarySurface::setPalette()
{
	if (s_surfaceDesc.ddpfPixelFormat.dwRGBBitCount <= 8)
	{
		HRESULT result = CompatDirectDrawSurface<IDirectDrawSurface7>::s_origVtable.SetPalette(
			g_frontBuffer, CompatPrimarySurface::palette);
		if (FAILED(result) && DDERR_NOPALETTEATTACHED != result)
		{
			LOG_ONCE("Failed to set the palette on the real primary surface: " << result);
		}
	}

	updatePalette(0, 256);
}

void RealPrimarySurface::update()
{
	if (!IsRectEmpty(&g_updateRect))
	{
		g_isFlipEvent = false;
		SetEvent(g_updateEvent);
	}
}

void RealPrimarySurface::updatePalette(DWORD startingEntry, DWORD count)
{
	CompatPaletteConverter::setPrimaryPalette(startingEntry, count);
	CompatGdi::updatePalette(startingEntry, count);
	invalidate(nullptr);
	updateNow();
}
