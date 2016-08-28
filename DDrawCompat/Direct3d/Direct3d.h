#pragma once

#include "CompatVtable.h"
#include "Direct3dVtblVisitor.h"

namespace Direct3d
{
	template <typename TDirect3d>
	class Direct3d : public CompatVtable<Direct3d<TDirect3d>, TDirect3d>
	{
	public:
		static void setCompatVtable(Vtable<TDirect3d>& vtable);
	};
}
