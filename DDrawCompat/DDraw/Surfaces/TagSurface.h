#pragma once

#include <ddraw.h>
#include <functional>

#include <Common/CompatRef.h>
#include <DDraw/Surfaces/Surface.h>

namespace DDraw
{
	class TagSurface : public Surface
	{
	public:
		TagSurface(DWORD origCaps, void* ddObject);
		virtual ~TagSurface() override;

		static TagSurface* get(CompatRef<IDirectDraw> dd);
		static TagSurface* findFullscreenWindow(HWND hwnd);

		static void forEachDirectDraw(std::function<void(CompatRef<IDirectDraw7>)> callback);

		void setFullscreenWindow(HWND hwnd);
		LONG setWindowStyle(LONG style);
		LONG setWindowExStyle(LONG exStyle);

	private:
		static HRESULT create(CompatRef<IDirectDraw> dd);

		void* m_ddObject;
		HWND m_fullscreenWindow;
		LONG m_fullscreenWindowStyle;
		LONG m_fullscreenWindowExStyle;
	};
}
