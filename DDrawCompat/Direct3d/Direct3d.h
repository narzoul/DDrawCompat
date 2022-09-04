#pragma once

#include <ddraw.h>

#include <Direct3d/Log.h>

namespace Direct3d
{
	void onCreateDevice(const IID& iid, IDirectDrawSurface7& surface);
	const IID& replaceDevice(const IID& iid);

	namespace Direct3d
	{
		template <typename Vtable>
		void hookVtable(const Vtable& vtable);
	}
}
