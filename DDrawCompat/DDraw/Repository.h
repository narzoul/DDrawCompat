#pragma once

#define CINTERFACE

#include <ddraw.h>

#include "CompatWeakPtr.h"

namespace DDraw
{
	namespace Repository
	{
		struct Surface
		{
			DDSURFACEDESC2 desc;
			CompatWeakPtr<IDirectDrawSurface7> surface;
		};

		class ScopedSurface : public Surface
		{
		public:
			ScopedSurface(const DDSURFACEDESC2& desc);
			~ScopedSurface();
		};

		CompatWeakPtr<IDirectDraw7> getDirectDraw();
	}
}
