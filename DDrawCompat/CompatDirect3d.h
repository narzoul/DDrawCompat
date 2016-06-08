#pragma once

#include "CompatVtable.h"
#include "Direct3dVtblVisitor.h"

template <typename TDirect3d>
class CompatDirect3d : public CompatVtable<CompatDirect3d<TDirect3d>, TDirect3d>
{
public:
	static void setCompatVtable(Vtable<TDirect3d>& vtable);
};
