#pragma once

#include <functional>

#include <Direct3d/Log.h>

namespace Direct3d
{
	namespace Direct3dDevice
	{
		void drawExecuteBufferPointPatches(std::function<void(const D3DPOINT&)> drawPoints);

		template <typename Vtable>
		void hookVtable(const Vtable& vtable);
	}
}
