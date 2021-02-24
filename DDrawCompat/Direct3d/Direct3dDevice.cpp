#include <Common/CompatPtr.h>
#include <Common/CompatRef.h>
#include <D3dDdi/Device.h>
#include <D3dDdi/ScopedCriticalSection.h>
#include <DDraw/Surfaces/Surface.h>
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

	template <typename TDirect3DDevice, typename TSurface>
	HRESULT STDMETHODCALLTYPE setRenderTarget(TDirect3DDevice* This, TSurface* lpNewRenderTarget, DWORD dwFlags)
	{
		HRESULT result = CompatVtable<Vtable<TDirect3DDevice>>::s_origVtable.SetRenderTarget(
			This, lpNewRenderTarget, dwFlags);
		if (DDERR_INVALIDPARAMS == result && lpNewRenderTarget)
		{
			auto surface = DDraw::Surface::getSurface(*lpNewRenderTarget);
			if (surface)
			{
				surface->setSizeOverride(1, 1);
				result = CompatVtable<Vtable<TDirect3DDevice>>::s_origVtable.SetRenderTarget(
					This, lpNewRenderTarget, dwFlags);
				surface->setSizeOverride(0, 0);
			}
		}
		return result;
	}

	void setCompatVtable(IDirect3DDeviceVtbl& vtable)
	{
		vtable.Execute = &execute;
	}

	template <typename TDirect3dDeviceVtbl>
	void setCompatVtable(TDirect3dDeviceVtbl& vtable)
	{
		vtable.SetRenderTarget = &setRenderTarget;
	}
}

namespace Direct3d
{
	template <typename TDirect3dDevice>
	void Direct3dDevice<TDirect3dDevice>::setCompatVtable(Vtable<TDirect3dDevice>& vtable)
	{
		::setCompatVtable(vtable);
	}

	template Direct3dDevice<IDirect3DDevice>;
	template Direct3dDevice<IDirect3DDevice2>;
	template Direct3dDevice<IDirect3DDevice3>;
	template Direct3dDevice<IDirect3DDevice7>;
}
