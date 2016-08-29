#pragma once

#include "CompatVtable.h"
#include "D3dDdiAdapterFuncsVisitor.h"

namespace D3dDdi
{
	class AdapterFuncs : public CompatVtable<AdapterFuncs, D3dDdiAdapterFuncsIntf>
	{
	public:
		static void setCompatVtable(D3DDDI_ADAPTERFUNCS& vtable);
	};
}
