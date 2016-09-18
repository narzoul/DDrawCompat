#pragma once

#include "Common/CompatVtable.h"
#include "D3dDdi/Visitors/AdapterFuncsVisitor.h"

namespace D3dDdi
{
	class AdapterFuncs : public CompatVtable<D3DDDI_ADAPTERFUNCS>
	{
	public:
		static void setCompatVtable(D3DDDI_ADAPTERFUNCS& vtable);
	};
}

SET_COMPAT_VTABLE(D3DDDI_ADAPTERFUNCS, D3dDdi::AdapterFuncs);
