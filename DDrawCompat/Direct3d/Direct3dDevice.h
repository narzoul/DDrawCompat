#pragma once

#include "Common/CompatVtable.h"
#include "Direct3d/Visitors/Direct3dDeviceVtblVisitor.h"

namespace Direct3d
{
	template <typename TDirect3dDevice>
	class Direct3dDevice : public CompatVtable<Direct3dDevice<TDirect3dDevice>, TDirect3dDevice>
	{
	public:
		static void setCompatVtable(Vtable<TDirect3dDevice>& vtable);
	};
}
