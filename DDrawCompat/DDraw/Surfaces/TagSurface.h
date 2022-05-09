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
		TagSurface(DWORD origCaps, void* ddObject) : Surface(origCaps), m_ddObject(ddObject) {}
		virtual ~TagSurface() override;

		static HRESULT create(CompatRef<IDirectDraw> dd);
		static void forEachDirectDraw(std::function<void(CompatRef<IDirectDraw7>)> callback);

	private:
		void* m_ddObject;
	};
}
