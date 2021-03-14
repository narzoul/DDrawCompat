#pragma once

#include <d3d.h>

#include <Direct3d/Log.h>

namespace Direct3d
{
	namespace Direct3dExecuteBuffer
	{
		void hookVtable(const IDirect3DExecuteBufferVtbl& vtable);
	}
}
