#pragma once

#include <d3d.h>

#include <Direct3d/Log.h>

namespace Direct3d
{
	D3DVERTEXBUFFERDESC getVertexBufferDesc();
	const IID& replaceDevice(const IID& iid);

	namespace Direct3d
	{
		template <typename Vtable>
		void hookVtable(const Vtable& vtable);
	}
}
