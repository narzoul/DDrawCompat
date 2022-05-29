#pragma once

#include <functional>

#include <ddraw.h>
#include <ddrawi.h>

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
		DDRAWI_DIRECTDRAW_INT& getInt(TDirectDraw& dd)
		{
			return reinterpret_cast<DDRAWI_DIRECTDRAW_INT&>(dd);
		}

		template <typename TDirectDraw>
		HWND* getDeviceWindowPtr(TDirectDraw& dd)
		{
			return &reinterpret_cast<HWND>(getInt(dd).lpLcl->hWnd);
		}

		template <typename Vtable>
		void hookVtable(const Vtable& vtable);
	}
}
