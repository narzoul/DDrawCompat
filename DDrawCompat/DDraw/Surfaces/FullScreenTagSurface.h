#pragma once

#include <functional>

#include "Common/CompatPtr.h"
#include "DDraw/Surfaces/Surface.h"

namespace DDraw
{
	class FullScreenTagSurface : public Surface
	{
	public:
		FullScreenTagSurface(const std::function<void()>& releaseHandler);
		virtual ~FullScreenTagSurface();

		static HRESULT create(CompatRef<IDirectDraw> dd, IDirectDrawSurface*& surface,
			const std::function<void()>& releaseHandler);

	private:
		std::function<void()> m_releaseHandler;
	};
}
