#pragma once

#include <Common/CompatVtable.h>
#include <Direct3d/Log.h>
#include <Direct3d/Visitors/Direct3dLightVtblVisitor.h>

namespace Direct3d
{
	class Direct3dLight : public CompatVtable<IDirect3DLightVtbl>
	{
	public:
		static void setCompatVtable(IDirect3DLightVtbl& vtable);
	};
}

SET_COMPAT_VTABLE(IDirect3DLightVtbl, Direct3d::Direct3dLight);
