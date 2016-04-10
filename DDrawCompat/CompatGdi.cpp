#include <atomic>
#include <cstring>

#include "CompatDirectDrawPalette.h"
#include "CompatDirectDrawSurface.h"
#include "CompatGdi.h"
#include "CompatGdiCaret.h"
#include "CompatGdiDcCache.h"
#include "CompatGdiDcFunctions.h"
#include "CompatGdiPaintHandlers.h"
#include "CompatGdiScrollFunctions.h"
#include "CompatGdiWinProc.h"
#include "CompatPaletteConverter.h"
#include "CompatPrimarySurface.h"
#include "DDrawProcs.h"
#include "RealPrimarySurface.h"

namespace
{
	CRITICAL_SECTION g_gdiCriticalSection;
	std::atomic<DWORD> g_renderingRefCount = 0;
	DWORD g_ddLockThreadRenderingRefCount = 0;
	DWORD g_ddLockThreadId = 0;
	HANDLE g_ddUnlockBeginEvent = nullptr;
	HANDLE g_ddUnlockEndEvent = nullptr;
	bool g_isDelayedUnlockPending = false;

	PALETTEENTRY g_usedPaletteEntries[256] = {};

	BOOL CALLBACK invalidateWindow(HWND hwnd, LPARAM lParam)
	{
		if (!IsWindowVisible(hwnd))
		{
			return TRUE;
		}

		DWORD processId = 0;
		GetWindowThreadProcessId(hwnd, &processId);
		if (processId != GetCurrentProcessId())
		{
			return TRUE;
		}

		if (lParam)
		{
			POINT origin = {};
			ClientToScreen(hwnd, &origin);
			RECT rect = *reinterpret_cast<const RECT*>(lParam);
			OffsetRect(&rect, -origin.x, -origin.y);
			RedrawWindow(hwnd, &rect, nullptr, RDW_ERASE | RDW_FRAME | RDW_INVALIDATE | RDW_ALLCHILDREN);
		}
		else
		{
			RedrawWindow(hwnd, nullptr, nullptr, RDW_ERASE | RDW_FRAME | RDW_INVALIDATE | RDW_ALLCHILDREN);
		}

		return TRUE;
	}

	bool lockPrimarySurface()
	{
		DDSURFACEDESC2 desc = {};
		desc.dwSize = sizeof(desc);
		if (FAILED(CompatDirectDrawSurface<IDirectDrawSurface7>::s_origVtable.Lock(
			CompatPrimarySurface::surface, nullptr, &desc, DDLOCK_WAIT, nullptr)))
		{
			return false;
		}

		g_ddLockThreadId = GetCurrentThreadId();
		CompatGdiDcCache::setDdLockThreadId(g_ddLockThreadId);
		CompatGdiDcCache::setSurfaceMemory(desc.lpSurface, desc.lPitch);
		return true;
	}

	/*
	Workaround for correctly drawing icons with a transparent background using BitBlt and a monochrome 
	bitmap mask. Normally black (index 0) and white (index 255) are selected as foreground and background
	colors on the target DC so that a BitBlt with the SRCAND ROP would preserve the background pixels and
	set the foreground pixels to 0. (Logical AND with 0xFF preserves all bits.)
	But if the physical palette contains another, earlier white entry, SRCAND will be performed with the
	wrong index (less than 0xFF), erasing some of the bits from all background pixels.
	This workaround replaces all unwanted white entries with a similar color.
	*/
	void replaceDuplicateWhitePaletteEntries(DWORD startingEntry, DWORD count)
	{
		if (startingEntry + count > 255)
		{
			count = 255 - startingEntry;
		}

		PALETTEENTRY entries[256] = {};
		std::memcpy(entries, &CompatPrimarySurface::paletteEntries[startingEntry], 
			count * sizeof(PALETTEENTRY));

		bool isReplacementDone = false;
		for (DWORD i = 0; i < count; ++i)
		{
			if (0xFF == entries[i].peRed && 0xFF == entries[i].peGreen && 0xFF == entries[i].peBlue)
			{
				entries[i].peRed = 0xFE;
				entries[i].peGreen = 0xFE;
				entries[i].peBlue = 0xFE;
				isReplacementDone = true;
			}
		}

		if (isReplacementDone)
		{
			CompatDirectDrawPalette::s_origVtable.SetEntries(
				CompatPrimarySurface::palette, 0, startingEntry, count, entries);
		}
	}

	void unlockPrimarySurface()
	{
		GdiFlush();
		CompatDirectDrawSurface<IDirectDrawSurface7>::s_origVtable.Unlock(
			CompatPrimarySurface::surface, nullptr);
		RealPrimarySurface::invalidate(nullptr);
		RealPrimarySurface::update();

		Compat::origProcs.ReleaseDDThreadLock();
	}
}

namespace CompatGdi
{
	CRITICAL_SECTION g_gdiCriticalSection;

	GdiScopedThreadLock::GdiScopedThreadLock() : m_isLocked(true)
	{
		EnterCriticalSection(&g_gdiCriticalSection);
	}

	GdiScopedThreadLock::~GdiScopedThreadLock()
	{
		unlock();
	}

	void GdiScopedThreadLock::unlock()
	{
		if (m_isLocked)
		{
			LeaveCriticalSection(&g_gdiCriticalSection);
			m_isLocked = false;
		}
	}

	bool beginGdiRendering()
	{
		if (!RealPrimarySurface::isFullScreen())
		{
			return false;
		}

		if (0 == g_renderingRefCount)
		{
			Compat::origProcs.AcquireDDThreadLock();
			EnterCriticalSection(&g_gdiCriticalSection);
			if (!lockPrimarySurface())
			{
				LeaveCriticalSection(&g_gdiCriticalSection);
				Compat::origProcs.ReleaseDDThreadLock();
				return false;
			}
		}
		else
		{
			EnterCriticalSection(&g_gdiCriticalSection);
		}

		if (GetCurrentThreadId() == g_ddLockThreadId)
		{
			++g_ddLockThreadRenderingRefCount;
		}

		++g_renderingRefCount;
		LeaveCriticalSection(&g_gdiCriticalSection);
		return true;
	}

	void endGdiRendering()
	{
		CompatGdi::GdiScopedThreadLock gdiLock;

		if (GetCurrentThreadId() == g_ddLockThreadId)
		{
			if (1 == g_renderingRefCount)
			{
				unlockPrimarySurface();
				g_ddLockThreadRenderingRefCount = 0;
				g_renderingRefCount = 0;
			}
			else if (1 == g_ddLockThreadRenderingRefCount)
			{
				g_isDelayedUnlockPending = true;
				gdiLock.unlock();
				WaitForSingleObject(g_ddUnlockBeginEvent, INFINITE);
				unlockPrimarySurface();
				g_ddLockThreadRenderingRefCount = 0;
				g_renderingRefCount = 0;
				SetEvent(g_ddUnlockEndEvent);
			}
			else
			{
				--g_ddLockThreadRenderingRefCount;
				--g_renderingRefCount;
			}
		}
		else
		{
			--g_renderingRefCount;
			if (1 == g_renderingRefCount && g_isDelayedUnlockPending)
			{
				SetEvent(g_ddUnlockBeginEvent);
				WaitForSingleObject(g_ddUnlockEndEvent, INFINITE);
				g_isDelayedUnlockPending = false;
			}
		}
	}

	void hookWndProc(LPCSTR className, WNDPROC &oldWndProc, WNDPROC newWndProc)
	{
		HWND hwnd = CreateWindow(className, nullptr, 0, 0, 0, 0, 0, nullptr, nullptr, nullptr, 0);
		oldWndProc = reinterpret_cast<WNDPROC>(
			SetClassLongPtr(hwnd, GCLP_WNDPROC, reinterpret_cast<LONG>(newWndProc)));
		DestroyWindow(hwnd);
	}

	void installHooks()
	{
		InitializeCriticalSection(&g_gdiCriticalSection);
		CompatPaletteConverter::init();
		if (CompatGdiDcCache::init())
		{
			g_ddUnlockBeginEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
			g_ddUnlockEndEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
			if (!g_ddUnlockBeginEvent || !g_ddUnlockEndEvent)
			{
				Compat::Log() << "Failed to create the unlock events for GDI";
				return;
			}

			CompatGdiDcFunctions::installHooks();
			CompatGdiPaintHandlers::installHooks();
			CompatGdiScrollFunctions::installHooks();
			CompatGdiWinProc::installHooks();
			CompatGdiCaret::installHooks();
		}
	}

	void invalidate(const RECT* rect)
	{
		EnumWindows(&invalidateWindow, reinterpret_cast<LPARAM>(rect));
	}

	void updatePalette(DWORD startingEntry, DWORD count)
	{
		GdiScopedThreadLock gdiLock;
		CompatGdiDcCache::clear();

		if (CompatPrimarySurface::palette)
		{
			replaceDuplicateWhitePaletteEntries(startingEntry, count);
			invalidate(nullptr);
		}
	}
}
