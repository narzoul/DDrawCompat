#pragma once

#include <functional>

#include <ddraw.h>

#include <Common/CompatPtr.h>
#include <Common/CompatRef.h>

namespace DDraw
{
	namespace DirectDraw
	{
		DDSURFACEDESC2 getDisplayMode(CompatRef<IDirectDraw7> dd);
		DDPIXELFORMAT getRgbPixelFormat(DWORD bpp);
		LRESULT handleActivateApp(bool isActivated, std::function<LRESULT()> callOrigWndProc);
		void onCreate(GUID* guid, CompatRef<IDirectDraw7> dd);
		void suppressEmulatedDirectDraw(GUID*& guid);

		template <typename TDirectDraw>
		void* getDdObject(TDirectDraw& dd)
		{
			return reinterpret_cast<void**>(&dd)[1];
		}

		template <typename TDirectDraw>
		HWND* getDeviceWindowPtr(TDirectDraw& dd)
		{
			return &reinterpret_cast<HWND**>(&dd)[1][8];
		}

		template <typename Vtable>
		void hookVtable(const Vtable& vtable);
	}
}
