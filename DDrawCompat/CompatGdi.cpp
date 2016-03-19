#include "CompatDirectDrawPalette.h"
#include "CompatDirectDrawSurface.h"
#include "CompatGdi.h"
#include "CompatGdiCaret.h"
#include "CompatGdiDcCache.h"
#include "CompatGdiDcFunctions.h"
#include "CompatGdiScrollFunctions.h"
#include "CompatGdiWinProc.h"
#include "CompatPrimarySurface.h"
#include "DDrawProcs.h"
#include "RealPrimarySurface.h"

namespace
{
	DWORD g_renderingRefCount = 0;
	DWORD g_ddLockThreadRenderingRefCount = 0;
	DWORD g_ddLockThreadId = 0;
	HANDLE g_ddUnlockBeginEvent = nullptr;
	HANDLE g_ddUnlockEndEvent = nullptr;
	bool g_isDelayedUnlockPending = false;

	bool g_isPaletteUsed = false;
	PALETTEENTRY g_usedPaletteEntries[256] = {};

	FARPROC getProcAddress(HMODULE module, const char* procName)
	{
		if (!module || !procName)
		{
			return nullptr;
		}

		PIMAGE_DOS_HEADER dosHeader = reinterpret_cast<PIMAGE_DOS_HEADER>(module);
		if (IMAGE_DOS_SIGNATURE != dosHeader->e_magic) {
			return nullptr;
		}
		char* moduleBase = reinterpret_cast<char*>(module);

		PIMAGE_NT_HEADERS ntHeader = reinterpret_cast<PIMAGE_NT_HEADERS>(
			reinterpret_cast<char*>(dosHeader) + dosHeader->e_lfanew);
		if (IMAGE_NT_SIGNATURE != ntHeader->Signature)
		{
			return nullptr;
		}

		PIMAGE_EXPORT_DIRECTORY exportDir = reinterpret_cast<PIMAGE_EXPORT_DIRECTORY>(
			moduleBase + ntHeader->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT].VirtualAddress);

		DWORD* rvaOfNames = reinterpret_cast<DWORD*>(moduleBase + exportDir->AddressOfNames);

		for (DWORD i = 0; i < exportDir->NumberOfNames; ++i)
		{
			if (0 == strcmp(procName, moduleBase + rvaOfNames[i]))
			{
				WORD* nameOrds = reinterpret_cast<WORD*>(moduleBase + exportDir->AddressOfNameOrdinals);
				DWORD* rvaOfFunctions = reinterpret_cast<DWORD*>(moduleBase + exportDir->AddressOfFunctions);
				return reinterpret_cast<FARPROC>(moduleBase + rvaOfFunctions[nameOrds[i]]);
			}
		}

		return nullptr;
	}

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
		Compat::origProcs.AcquireDDThreadLock();

		DDSURFACEDESC2 desc = {};
		desc.dwSize = sizeof(desc);
		if (FAILED(CompatDirectDrawSurface<IDirectDrawSurface7>::s_origVtable.Lock(
			CompatPrimarySurface::surface, nullptr, &desc, DDLOCK_WAIT, nullptr)))
		{
			Compat::origProcs.ReleaseDDThreadLock();
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
	void replaceDuplicateWhitePaletteEntries(const PALETTEENTRY (&entries)[256])
	{
		PALETTEENTRY newEntries[256] = {};
		memcpy(newEntries, entries, sizeof(entries));

		bool isReplacementDone = false;
		for (int i = 0; i < 255; ++i)
		{
			if (0xFF == entries[i].peRed && 0xFF == entries[i].peGreen && 0xFF == entries[i].peBlue)
			{
				newEntries[i].peRed = 0xFE;
				newEntries[i].peGreen = 0xFE;
				newEntries[i].peBlue = 0xFE;
				isReplacementDone = true;
			}
		}

		if (isReplacementDone)
		{
			CompatDirectDrawPalette::s_origVtable.SetEntries(
				CompatPrimarySurface::palette, 0, 0, 256, newEntries);
		}
	}

	void unlockPrimarySurface()
	{
		GdiFlush();
		CompatDirectDrawSurface<IDirectDrawSurface7>::s_origVtable.Unlock(
			CompatPrimarySurface::surface, nullptr);
		RealPrimarySurface::update();

		Compat::origProcs.ReleaseDDThreadLock();
	}
}

namespace CompatGdi
{
	CRITICAL_SECTION g_gdiCriticalSection;
	std::unordered_map<void*, const char*> g_funcNames;

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

		GdiScopedThreadLock gdiLock;

		if (0 == g_renderingRefCount)
		{
			if (!lockPrimarySurface())
			{
				return false;
			}
			++g_ddLockThreadRenderingRefCount;
		}
		else if (GetCurrentThreadId() == g_ddLockThreadId)
		{
			++g_ddLockThreadRenderingRefCount;
		}

		if (!g_isPaletteUsed && CompatPrimarySurface::palette)
		{
			g_isPaletteUsed = true;
			ZeroMemory(g_usedPaletteEntries, sizeof(g_usedPaletteEntries));
			CompatPrimarySurface::palette->lpVtbl->GetEntries(
				CompatPrimarySurface::palette, 0, 0, 256, g_usedPaletteEntries);
		}

		++g_renderingRefCount;
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

	void hookGdiFunction(const char* moduleName, const char* funcName, void*& origFuncPtr, void* newFuncPtr)
	{
#ifdef _DEBUG
		g_funcNames[origFuncPtr] = funcName;
#endif

		FARPROC procAddr = getProcAddress(GetModuleHandle(moduleName), funcName);
		if (!procAddr)
		{
			Compat::Log() << "Failed to load the address of a GDI function: " << funcName;
			return;
		}

		origFuncPtr = procAddr;
		if (NO_ERROR != DetourAttach(&origFuncPtr, newFuncPtr))
		{
			Compat::Log() << "Failed to hook a GDI function: " << funcName;
			return;
		}
	}

	void installHooks()
	{
		InitializeCriticalSection(&g_gdiCriticalSection);
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
			CompatGdiScrollFunctions::installHooks();
			CompatGdiWinProc::installHooks();
			CompatGdiCaret::installHooks();
		}
	}

	void invalidate(const RECT* rect)
	{
		EnumWindows(&invalidateWindow, reinterpret_cast<LPARAM>(rect));
	}

	void updatePalette()
	{
		GdiScopedThreadLock gdiLock;
		CompatGdiDcCache::clear();

		if (g_isPaletteUsed && CompatPrimarySurface::palette)
		{
			g_isPaletteUsed = false;

			PALETTEENTRY usedPaletteEntries[256] = {};
			CompatPrimarySurface::palette->lpVtbl->GetEntries(
				CompatPrimarySurface::palette, 0, 0, 256, usedPaletteEntries);

			if (0 != memcmp(usedPaletteEntries, g_usedPaletteEntries, sizeof(usedPaletteEntries)))
			{
				replaceDuplicateWhitePaletteEntries(usedPaletteEntries);
				invalidate(nullptr);
			}
		}
	}
}
