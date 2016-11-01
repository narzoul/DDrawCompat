#pragma once

#include "Common/CompatPtr.h"
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
		static const DDSURFACEDESC2& getDesc();
		static CompatPtr<IDirectDrawSurface7> getGdiSurface();
		static CompatPtr<IDirectDrawSurface7> getPrimary();

		void updateGdiSurfacePtr(IDirectDrawSurface* flipTargetOverride);

		static CompatWeakPtr<IDirectDrawPalette> s_palette;
		static PALETTEENTRY s_paletteEntries[256];

	private:
		PrimarySurface(Surface* surface);

		virtual void createImpl() override;

		std::unique_ptr<Surface> m_surface;
	};
}
