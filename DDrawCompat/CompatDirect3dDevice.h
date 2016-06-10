#pragma once

#include "CompatVtable.h"
#include "Direct3dDeviceVtblVisitor.h"

template <typename TDirect3dDevice>
class CompatDirect3dDevice : public CompatVtable<CompatDirect3dDevice<TDirect3dDevice>, TDirect3dDevice>
{
public:
	static void setCompatVtable(Vtable<TDirect3dDevice>& vtable);
};
