#pragma once

#include "Common/CompatVtable.h"
#include "D3dDdi/Visitors/AdapterCallbacksVisitor.h"

namespace D3dDdi
{
	class AdapterCallbacks :
		public CompatVtable<AdapterCallbacks, AdapterCallbacksIntf>
	{
	public:
		static void setCompatVtable(D3DDDI_ADAPTERCALLBACKS& vtable);
	};
}
