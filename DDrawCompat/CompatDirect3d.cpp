#include "CompatDepthBuffer.h"
#include "CompatDirect3d.h"
#include "CompatPtr.h"
#include "Direct3dTypes.h"

namespace
{
	template <typename TDirect3d>
	struct EnumDevicesParams
	{
		CompatPtr<TDirect3d> d3d;
		typename Types<TDirect3d>::TD3dEnumDevicesCallback enumDevicesCallback;
		void* userArg;
	};

	HRESULT CALLBACK d3dEnumDevicesCallback(
		GUID* lpGuid,
		LPSTR lpDeviceDescription,
		LPSTR lpDeviceName,
		LPD3DDEVICEDESC lpD3DHWDeviceDesc,
		LPD3DDEVICEDESC lpD3DHELDeviceDesc,
		LPVOID lpContext)
	{
		auto& params = *reinterpret_cast<EnumDevicesParams<IDirect3D3>*>(lpContext);
		CompatDepthBuffer::fixSupportedZBufferBitDepths<IDirect3D3>(params.d3d, *lpD3DHWDeviceDesc);
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
		CompatDepthBuffer::fixSupportedZBufferBitDepths<IDirect3D7>(params.d3d, *lpD3DDeviceDesc);
		return params.enumDevicesCallback(lpDeviceDescription, lpDeviceName,
			lpD3DDeviceDesc, params.userArg);
	}

	template <typename TDirect3d, typename TD3dEnumDevicesCallback>
	HRESULT STDMETHODCALLTYPE enumDevices(
		TDirect3d* This, TD3dEnumDevicesCallback lpEnumDevicesCallback, LPVOID lpUserArg)
	{
		if (!lpEnumDevicesCallback)
		{
			return CompatVtableBase<TDirect3d>::s_origVtable.EnumDevices(
				This, lpEnumDevicesCallback, lpUserArg);
		}

		typedef typename Types<TDirect3d>::TDirect3dHighest TDirect3dHighest;
		CompatPtr<TDirect3dHighest> d3d(Compat::queryInterface<TDirect3dHighest>(This));

		EnumDevicesParams<TDirect3dHighest> params = { d3d, lpEnumDevicesCallback, lpUserArg };
		return CompatVtableBase<TDirect3d>::s_origVtable.EnumDevices(
			This, &d3dEnumDevicesCallback, &params);
	}
}

template <typename TDirect3d>
void CompatDirect3d<TDirect3d>::setCompatVtable(Vtable<TDirect3d>& vtable)
{
	vtable.EnumDevices = &enumDevices;
	// No need to fix FindDevice since it uses EnumDevices
}

template CompatDirect3d<IDirect3D>;
template CompatDirect3d<IDirect3D2>;
template CompatDirect3d<IDirect3D3>;
template CompatDirect3d<IDirect3D7>;
