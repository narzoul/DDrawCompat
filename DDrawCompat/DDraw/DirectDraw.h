#pragma once

#include <ddraw.h>

#include <Common/CompatPtr.h>
#include <Common/CompatRef.h>

namespace DDraw
{
	namespace DirectDraw
	{
		DDSURFACEDESC2 getDisplayMode(CompatRef<IDirectDraw7> dd);
		DDPIXELFORMAT getRgbPixelFormat(DWORD bpp);
		void onCreate(GUID* guid, CompatRef<IDirectDraw7> dd);
		void suppressEmulatedDirectDraw(GUID*& guid);

		template <typename TDirectDraw>
		HWND* getDeviceWindowPtr(TDirectDraw& dd)
		{
			return &reinterpret_cast<HWND**>(&dd)[1][8];
		}

		template <typename Vtable>
		void hookVtable(const Vtable& vtable);
	}
}
