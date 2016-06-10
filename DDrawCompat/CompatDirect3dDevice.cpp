#include "CompatDirect3dDevice.h"

template <typename TDirect3dDevice>
void CompatDirect3dDevice<TDirect3dDevice>::setCompatVtable(Vtable<TDirect3dDevice>& /*vtable*/)
{
}

template CompatDirect3dDevice<IDirect3DDevice>;
template CompatDirect3dDevice<IDirect3DDevice2>;
template CompatDirect3dDevice<IDirect3DDevice3>;
template CompatDirect3dDevice<IDirect3DDevice7>;
