#include <Common/CompatPtr.h>
#include <Direct3d/Direct3d.h>
#include <Direct3d/Direct3dDevice.h>
#include <Direct3d/Types.h>

namespace
{
	template <typename TDirect3d, typename TDirectDrawSurface, typename TDirect3dDevice, typename... Params>
	HRESULT STDMETHODCALLTYPE createDevice(
		TDirect3d* This,
		REFCLSID rclsid,
		TDirectDrawSurface* lpDDS,
		TDirect3dDevice** lplpD3DDevice,
		Params... params)
	{
		HRESULT result = CompatVtable<Vtable<TDirect3d>>::s_origVtable.CreateDevice(
			This, (IID_IDirect3DRampDevice == rclsid) ? IID_IDirect3DRGBDevice : rclsid, lpDDS, lplpD3DDevice, params...);
		if (SUCCEEDED(result))
		{
			CompatVtable<Vtable<TDirect3dDevice>>::hookVtable((*lplpD3DDevice)->lpVtbl);
		}
		return result;
	}

	void setCompatVtable2(IDirect3DVtbl& /*vtable*/)
	{
	}

	template <typename TDirect3dVtbl>
	void setCompatVtable2(TDirect3dVtbl& vtable)
	{
		vtable.CreateDevice = &createDevice;
	}
}

namespace Direct3d
{
	template <typename TDirect3d>
	void Direct3d<TDirect3d>::setCompatVtable(Vtable<TDirect3d>& vtable)
	{
		setCompatVtable2(vtable);
	}

	template Direct3d<IDirect3D>;
	template Direct3d<IDirect3D2>;
	template Direct3d<IDirect3D3>;
	template Direct3d<IDirect3D7>;
}
