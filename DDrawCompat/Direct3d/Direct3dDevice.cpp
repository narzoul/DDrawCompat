#include <Common/CompatPtr.h>
#include <Common/CompatRef.h>
#include <D3dDdi/Device.h>
#include <Direct3d/DepthBuffer.h>
#include <Direct3d/Direct3dDevice.h>
#include <Direct3d/Types.h>

namespace
{
	HRESULT STDMETHODCALLTYPE execute(IDirect3DDevice* This,
		LPDIRECT3DEXECUTEBUFFER lpDirect3DExecuteBuffer, LPDIRECT3DVIEWPORT lpDirect3DViewport, DWORD dwFlags)
	{
		D3dDdi::ScopedCriticalSection lock;
		D3dDdi::Device::enableFlush(false);
		HRESULT result = CompatVtable<IDirect3DDeviceVtbl>::s_origVtable.Execute(
			This, lpDirect3DExecuteBuffer, lpDirect3DViewport, dwFlags);
		D3dDdi::Device::enableFlush(true);
		return result;
	}

	template <typename TDirect3dDevice, typename TD3dDeviceDesc>
	void fixSupportedZBufferBitDepths(CompatRef<TDirect3dDevice> d3dDevice, TD3dDeviceDesc& desc)
	{
		typedef typename Direct3d::Types<TDirect3dDevice>::TDirect3d TDirect3d;
		CompatPtr<TDirect3d> d3d;
		if (SUCCEEDED(CompatVtable<Vtable<TDirect3dDevice>>::s_origVtable.GetDirect3D(
			&d3dDevice, &d3d.getRef())))
		{
			typedef typename Direct3d::Types<TDirect3dDevice>::TDirect3dHighest TDirect3dHighest;
			Direct3d::DepthBuffer::fixSupportedZBufferBitDepths<TDirect3dHighest>(d3d, desc);
		}
	}

	template <typename TDirect3dDevice, typename TD3dDeviceDesc, typename... Params>
	HRESULT STDMETHODCALLTYPE getCaps(
		TDirect3dDevice* This,
		TD3dDeviceDesc* lpD3DHWDevDesc,
		Params... params)
	{
		HRESULT result = CompatVtable<Vtable<TDirect3dDevice>>::s_origVtable.GetCaps(
			This, lpD3DHWDevDesc, params...);
		if (SUCCEEDED(result))
		{
			fixSupportedZBufferBitDepths<TDirect3dDevice>(*This, *lpD3DHWDevDesc);
		}
		return result;
	}

	void setCompatVtable(IDirect3DDeviceVtbl& vtable)
	{
		vtable.Execute = &execute;
	}

	template <typename TDirect3dDeviceVtbl>
	void setCompatVtable(TDirect3dDeviceVtbl& /*vtable*/)
	{
	}
}

namespace Direct3d
{
	template <typename TDirect3dDevice>
	void Direct3dDevice<TDirect3dDevice>::setCompatVtable(Vtable<TDirect3dDevice>& vtable)
	{
		::setCompatVtable(vtable);
		vtable.GetCaps = &getCaps;
	}

	template Direct3dDevice<IDirect3DDevice>;
	template Direct3dDevice<IDirect3DDevice2>;
	template Direct3dDevice<IDirect3DDevice3>;
	template Direct3dDevice<IDirect3DDevice7>;
}
