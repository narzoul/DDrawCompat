#pragma once

#include <ddraw.h>
#include <ddrawi.h>

#include <Common/CompatRef.h>
#include <DDraw/Surfaces/Surface.h>

namespace DDraw
{
	class TagSurface : public Surface
	{
	public:
		TagSurface(DWORD origCaps, DDRAWI_DIRECTDRAW_LCL* ddLcl);
		virtual ~TagSurface() override;

		static TagSurface* get(DDRAWI_DIRECTDRAW_LCL* ddLcl);
		static TagSurface* get(CompatRef<IDirectDraw> dd);
		static TagSurface* findFullscreenWindow(HWND hwnd = nullptr);

		CompatPtr<IDirectDraw7> getDD();
		bool isFullscreen() const { return m_fullscreenWindow; }
		void setFullscreenWindow(HWND hwnd);
		LONG setWindowStyle(LONG style);
		LONG setWindowExStyle(LONG exStyle);

	private:
		static HRESULT create(CompatRef<IDirectDraw> dd);

		DDRAWI_DIRECTDRAW_INT m_ddInt;
		HWND m_fullscreenWindow;
		LONG m_fullscreenWindowStyle;
		LONG m_fullscreenWindowExStyle;
	};
}
