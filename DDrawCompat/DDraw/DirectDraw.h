#pragma once

#include <functional>
#include <type_traits>

#include <ddraw.h>
#include <ddrawi.h>

#include <Common/CompatPtr.h>
#include <Common/CompatRef.h>

namespace DDraw
{
	namespace DirectDraw
	{
		template <typename TSurface>
		struct IsDirectDraw : std::false_type {};

		template <> struct IsDirectDraw<IDirectDraw> : std::true_type {};
		template <> struct IsDirectDraw<IDirectDraw2> : std::true_type {};
		template <> struct IsDirectDraw<IDirectDraw4> : std::true_type {};
		template <> struct IsDirectDraw<IDirectDraw7> : std::true_type {};

		DDPIXELFORMAT getRgbPixelFormat(DWORD bpp);
		void hookDDrawWindowProc(WNDPROC ddrawWndProc);
		void onCreate(GUID* guid, CompatRef<IDirectDraw7> dd);
		void suppressEmulatedDirectDraw(GUID*& guid);

		template <typename TDirectDraw, typename = std::enable_if_t<IsDirectDraw<TDirectDraw>::value>>
		DDRAWI_DIRECTDRAW_INT& getInt(TDirectDraw& dd)
		{
			return reinterpret_cast<DDRAWI_DIRECTDRAW_INT&>(dd);
		}

		template <typename TDirectDraw, typename = std::enable_if_t<IsDirectDraw<TDirectDraw>::value>>
		HWND* getDeviceWindowPtr(TDirectDraw& dd)
		{
			return &reinterpret_cast<HWND>(getInt(dd).lpLcl->hWnd);
		}

		template <typename Vtable>
		void hookVtable(const Vtable& vtable);
	}
}
