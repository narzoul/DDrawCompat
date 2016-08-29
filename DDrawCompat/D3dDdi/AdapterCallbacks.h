#pragma once

#include "CompatVtable.h"
#include "D3dDdiAdapterCallbacksVisitor.h"

namespace D3dDdi
{
	class AdapterCallbacks :
		public CompatVtable<AdapterCallbacks, D3dDdiAdapterCallbacksIntf>
	{
	public:
		static void setCompatVtable(D3DDDI_ADAPTERCALLBACKS& vtable);
	};
}
