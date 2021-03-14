#pragma once

#include <d3d.h>
#include <d3dumddi.h>

#include <Common/VtableVisitor.h>

template <>
struct VtableForEach<D3DDDI_ADAPTERCALLBACKS>
{
	template <typename Vtable, typename Visitor>
	static void forEach(Visitor& visitor, UINT /*version*/)
	{
		DD_VISIT(pfnQueryAdapterInfoCb);
		DD_VISIT(pfnGetMultisampleMethodListCb);
	}
};
