#include "Common/CompatPtr.h"
#include "Direct3d/DepthBuffer.h"
#include "Direct3d/Direct3d.h"
#include "Direct3d/Direct3dDevice.h"
#include "Direct3d/Types.h"

namespace
{
	template <typename TDirect3d>
	struct EnumDevicesParams
	{
		CompatPtr<TDirect3d> d3d;
		typename Direct3d::Types<TDirect3d>::TD3dEnumDevicesCallback enumDevicesCallback;
		void* userArg;
	};

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

	HRESULT CALLBACK d3dEnumDevicesCallback(
		GUID* lpGuid,
		LPSTR lpDeviceDescription,
		LPSTR lpDeviceName,
		LPD3DDEVICEDESC lpD3DHWDeviceDesc,
		LPD3DDEVICEDESC lpD3DHELDeviceDesc,
		LPVOID lpContext)
	{
		auto& params = *reinterpret_cast<EnumDevicesParams<IDirect3D3>*>(lpContext);
		Direct3d::DepthBuffer::fixSupportedZBufferBitDepths<IDirect3D3>(params.d3d, *lpD3DHWDeviceDesc);
		return params.enumDevicesCallback(lpGuid, lpDeviceDescription, lpDeviceName,
			lpD3DHWDeviceDesc, lpD3DHELDeviceDesc, params.userArg);
	}

	HRESULT CALLBACK d3dEnumDevicesCallback(
		LPSTR lpDeviceDescription,
		LPSTR lpDeviceName,
		LPD3DDEVICEDESC7 lpD3DDeviceDesc,
		LPVOID lpContext)
	{
		auto& params = *reinterpret_cast<EnumDevicesParams<IDirect3D7>*>(lpContext);
		Direct3d::DepthBuffer::fixSupportedZBufferBitDepths<IDirect3D7>(params.d3d, *lpD3DDeviceDesc);
		return params.enumDevicesCallback(lpDeviceDescription, lpDeviceName,
			lpD3DDeviceDesc, params.userArg);
	}

	template <typename TDirect3d, typename TD3dEnumDevicesCallback>
	HRESULT STDMETHODCALLTYPE enumDevices(
		TDirect3d* This, TD3dEnumDevicesCallback lpEnumDevicesCallback, LPVOID lpUserArg)
	{
		if (!lpEnumDevicesCallback)
		{
			return CompatVtable<Vtable<TDirect3d>>::s_origVtable.EnumDevices(
				This, lpEnumDevicesCallback, lpUserArg);
		}

		typedef typename Direct3d::Types<TDirect3d>::TDirect3dHighest TDirect3dHighest;
		CompatPtr<TDirect3dHighest> d3d(Compat::queryInterface<TDirect3dHighest>(This));

		EnumDevicesParams<TDirect3dHighest> params = { d3d, lpEnumDevicesCallback, lpUserArg };
		return CompatVtable<Vtable<TDirect3d>>::s_origVtable.EnumDevices(
			This, &d3dEnumDevicesCallback, &params);
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
		vtable.EnumDevices = &enumDevices;
		// No need to fix FindDevice since it uses EnumDevices
		setCompatVtable2(vtable);
	}

	template Direct3d<IDirect3D>;
	template Direct3d<IDirect3D2>;
	template Direct3d<IDirect3D3>;
	template Direct3d<IDirect3D7>;
}
