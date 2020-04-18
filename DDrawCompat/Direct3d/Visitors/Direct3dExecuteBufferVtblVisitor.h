#pragma once

#include <d3d.h>

#include <Common/VtableVisitor.h>

template <>
struct VtableForEach<IDirect3DExecuteBufferVtbl>
{
	template <typename Vtable, typename Visitor>
	static void forEach(Visitor& visitor)
	{
		VtableForEach<IUnknownVtbl>::forEach<Vtable>(visitor);

        DD_VISIT(Initialize);
        DD_VISIT(Lock);
        DD_VISIT(Unlock);
        DD_VISIT(SetExecuteData);
        DD_VISIT(GetExecuteData);
        DD_VISIT(Validate);
        DD_VISIT(Optimize);
    }
};
