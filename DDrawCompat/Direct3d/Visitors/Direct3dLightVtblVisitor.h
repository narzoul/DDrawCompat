#pragma once

#include <d3d.h>

#include <Common/VtableVisitor.h>

template <>
struct VtableForEach<IDirect3DLightVtbl>
{
	template <typename Vtable, typename Visitor>
	static void forEach(Visitor& visitor)
	{
		VtableForEach<IUnknownVtbl>::forEach<Vtable>(visitor);

		DD_VISIT(Initialize);
		DD_VISIT(SetLight);
		DD_VISIT(GetLight);
	}
};
