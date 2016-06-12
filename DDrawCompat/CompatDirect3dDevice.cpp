#include "CompatDepthBuffer.h"
#include "CompatDirect3dDevice.h"
#include "CompatPtr.h"
#include "CompatRef.h"
#include "Direct3dTypes.h"

namespace
{
	template <typename TDirect3dDevice, typename TD3dDeviceDesc>
	void fixSupportedZBufferBitDepths(CompatRef<TDirect3dDevice> d3dDevice, TD3dDeviceDesc& desc)
	{
		typedef typename Types<TDirect3dDevice>::TDirect3d TDirect3d;
		CompatPtr<TDirect3d> d3d;
		if (SUCCEEDED(CompatVtableBase<TDirect3dDevice>::s_origVtable.GetDirect3D(
			&d3dDevice, &d3d.getRef())))
		{
			typedef typename Types<TDirect3dDevice>::TDirect3dHighest TDirect3dHighest;
			CompatDepthBuffer::fixSupportedZBufferBitDepths<TDirect3dHighest>(d3d, desc);
		}
	}

	template <typename TDirect3dDevice, typename TD3dDeviceDesc, typename... Params>
	HRESULT STDMETHODCALLTYPE getCaps(
		TDirect3dDevice* This,
		TD3dDeviceDesc* lpD3DHWDevDesc,
		Params... params)
	{
		HRESULT result = CompatVtableBase<TDirect3dDevice>::s_origVtable.GetCaps(
			This, lpD3DHWDevDesc, params...);
		if (SUCCEEDED(result))
		{
			fixSupportedZBufferBitDepths<TDirect3dDevice>(*This, *lpD3DHWDevDesc);
		}
		return result;
	}
}

template <typename TDirect3dDevice>
void CompatDirect3dDevice<TDirect3dDevice>::setCompatVtable(Vtable<TDirect3dDevice>& vtable)
{
	vtable.GetCaps = &getCaps;
}

template CompatDirect3dDevice<IDirect3DDevice>;
template CompatDirect3dDevice<IDirect3DDevice2>;
template CompatDirect3dDevice<IDirect3DDevice3>;
template CompatDirect3dDevice<IDirect3DDevice7>;
