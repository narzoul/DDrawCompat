#include "CompatDirect3d.h"

template <typename TDirect3d>
void CompatDirect3d<TDirect3d>::setCompatVtable(Vtable<TDirect3d>& /*vtable*/)
{
}

template CompatDirect3d<IDirect3D>;
template CompatDirect3d<IDirect3D2>;
template CompatDirect3d<IDirect3D3>;
template CompatDirect3d<IDirect3D7>;
