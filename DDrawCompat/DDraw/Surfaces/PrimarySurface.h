#pragma once

#include <ddraw.h>

#include "Common/CompatPtr.h"
#include "Common/CompatRef.h"
#include "DDraw/Surfaces/Surface.h"

namespace DDraw
{
	class PrimarySurface : public Surface
	{
	public:
		virtual ~PrimarySurface();

		template <typename TDirectDraw, typename TSurface, typename TSurfaceDesc>
		static HRESULT create(CompatRef<TDirectDraw> dd, TSurfaceDesc desc, TSurface*& surface);

		static HRESULT flipToGdiSurface();
		static CompatPtr<IDirectDrawSurface7> getGdiSurface();
		static CompatPtr<IDirectDrawSurface7> getBackBuffer();
		static CompatPtr<IDirectDrawSurface7> getLastSurface();
		static CompatWeakPtr<IDirectDrawSurface7> getPrimary();
		static DWORD getOrigCaps();
		static void onRestore();
		static void updatePalette();

		template <typename TSurface>
		static bool isGdiSurface(TSurface* surface);

		static CompatWeakPtr<IDirectDrawPalette> s_palette;
		static PALETTEENTRY s_paletteEntries[256];

	private:
		PrimarySurface(Surface* surface);

		virtual void createImpl() override;

		std::unique_ptr<Surface> m_surface;
	};
}
