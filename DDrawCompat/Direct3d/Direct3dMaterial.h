#pragma once

#include <Common/CompatVtable.h>
#include <Direct3d/Log.h>
#include <Direct3d/Visitors/Direct3dMaterialVtblVisitor.h>

namespace Direct3d
{
	template <typename TDirect3dMaterial>
	class Direct3dMaterial : public CompatVtable<Vtable<TDirect3dMaterial>>
	{
	public:
		static void setCompatVtable(Vtable<TDirect3dMaterial>& vtable);
	};
}

SET_COMPAT_VTABLE(IDirect3DMaterialVtbl, Direct3d::Direct3dMaterial<IDirect3DMaterial>);
SET_COMPAT_VTABLE(IDirect3DMaterial2Vtbl, Direct3d::Direct3dMaterial<IDirect3DMaterial2>);
SET_COMPAT_VTABLE(IDirect3DMaterial3Vtbl, Direct3d::Direct3dMaterial<IDirect3DMaterial3>);
