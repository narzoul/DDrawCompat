#pragma once

#include <ddraw.h>

#include <Common/CompatPtr.h>
#include <Common/CompatRef.h>
#include <DDraw/Surfaces/Surface.h>

namespace DDraw
{
	class PalettizedTexture : public Surface
	{
	public:
		PalettizedTexture(DWORD origFlags, DWORD origCaps) : Surface(origFlags, origCaps) {}
		virtual ~PalettizedTexture() override;

		template <typename TDirectDraw, typename TSurface, typename TSurfaceDesc>
		static HRESULT create(CompatRef<TDirectDraw> dd, TSurfaceDesc desc, TSurface*& surface);

	private:
		CompatPtr<IDirectDrawSurface7> m_palettizedSurface;

		virtual void createImpl() override;
	};
}
