#pragma once

#include <Common/CompatRef.h>
#include <Common/CompatVtable.h>
#include <DDraw/Visitors/DirectDrawClipperVtblVisitor.h>

namespace DDraw
{
	class DirectDrawClipper : public CompatVtable<IDirectDrawClipperVtbl>
	{
	public:
		static HRGN getClipRgn(CompatRef<IDirectDrawClipper> clipper);
		static HRESULT setClipRgn(CompatRef<IDirectDrawClipper> clipper, HRGN rgn);
		static void update();

		static void setCompatVtable(IDirectDrawClipperVtbl& vtable);
	};
}

SET_COMPAT_VTABLE(IDirectDrawClipperVtbl, DDraw::DirectDrawClipper);
