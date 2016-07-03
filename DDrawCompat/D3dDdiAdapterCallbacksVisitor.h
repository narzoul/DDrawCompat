#pragma once

#define CINTERFACE

#include <d3d.h>
#include <d3dumddi.h>

#include "DDrawVtableVisitor.h"

struct D3dDdiAdapterCallbacksIntf
{
	D3DDDI_ADAPTERCALLBACKS* lpVtbl;
};

template <>
struct DDrawVtableForEach<D3DDDI_ADAPTERCALLBACKS>
{
	template <typename Vtable, typename Visitor>
	static void forEach(Visitor& visitor)
	{
		DD_VISIT(pfnQueryAdapterInfoCb);
		// DD_VISIT(pfnGetMultisampleMethodListCb);   -- not set by ddraw, potentially garbage
	}
};
