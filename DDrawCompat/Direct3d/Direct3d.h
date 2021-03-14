#pragma once

#include <Direct3d/Log.h>

namespace Direct3d
{
	namespace Direct3d
	{
		template <typename Vtable>
		void hookVtable(const Vtable& vtable);
	}
}
