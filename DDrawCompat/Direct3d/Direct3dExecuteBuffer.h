#pragma once

#include <Common/CompatVtable.h>
#include <Direct3d/Log.h>
#include <Direct3d/Visitors/Direct3dExecuteBufferVtblVisitor.h>

namespace Direct3d
{
	class Direct3dExecuteBuffer : public CompatVtable<IDirect3DExecuteBufferVtbl>
	{
	public:
		static void setCompatVtable(IDirect3DExecuteBufferVtbl& vtable);
	};
}

SET_COMPAT_VTABLE(IDirect3DExecuteBufferVtbl, Direct3d::Direct3dExecuteBuffer);
