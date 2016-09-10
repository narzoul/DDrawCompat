#pragma once

#include "Common/CompatRef.h"
#include "Common/CompatVtable.h"
#include "DDraw/Visitors/DirectDrawSurfaceVtblVisitor.h"
#include "DDraw/Types.h"

namespace DDraw
{
	template <typename TSurface>
	class DirectDrawSurface : public CompatVtable<DirectDrawSurface<TSurface>, TSurface>
	{
	public:
		typedef typename Types<TSurface>::TSurfaceDesc TSurfaceDesc;

		static void setCompatVtable(Vtable<TSurface>& vtable);

	};
}
