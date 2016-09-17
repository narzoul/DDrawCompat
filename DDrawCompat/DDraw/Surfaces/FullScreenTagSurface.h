#pragma once

#include "DDraw/Surfaces/Surface.h"

namespace DDraw
{
	class FullScreenTagSurface : public Surface
	{
	public:
		virtual ~FullScreenTagSurface();

		static HRESULT create(CompatRef<IDirectDraw> dd);
		static void destroy();
		static CompatPtr<IDirectDraw7> getFullScreenDirectDraw();
		static CompatPtr<IDirectDrawSurface7> getFullScreenTagSurface();
	};
}
