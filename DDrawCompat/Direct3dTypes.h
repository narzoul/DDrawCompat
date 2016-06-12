#pragma once

#define CINTERFACE

#include <d3d.h>

struct Direct3dTypes
{
	typedef IDirect3D TDirect3d;
	typedef IDirect3D3 TDirect3dHighest;
	typedef IDirect3DDevice TDirect3dDevice;
	typedef D3DDEVICEDESC TD3dDeviceDesc;
	typedef LPD3DENUMDEVICESCALLBACK TD3dEnumDevicesCallback;
};

struct Direct3dTypes2
{
	typedef IDirect3D2 TDirect3d;
	typedef IDirect3D3 TDirect3dHighest;
	typedef IDirect3DDevice2 TDirect3dDevice;
	typedef D3DDEVICEDESC TD3dDeviceDesc;
	typedef LPD3DENUMDEVICESCALLBACK TD3dEnumDevicesCallback;
};

struct Direct3dTypes3
{
	typedef IDirect3D3 TDirect3d;
	typedef IDirect3D3 TDirect3dHighest;
	typedef IDirect3DDevice3 TDirect3dDevice;
	typedef D3DDEVICEDESC TD3dDeviceDesc;
	typedef LPD3DENUMDEVICESCALLBACK TD3dEnumDevicesCallback;
};

struct Direct3dTypes7
{
	typedef IDirect3D7 TDirect3d;
	typedef IDirect3D7 TDirect3dHighest;
	typedef IDirect3DDevice7 TDirect3dDevice;
	typedef D3DDEVICEDESC7 TD3dDeviceDesc;
	typedef LPD3DENUMDEVICESCALLBACK7 TD3dEnumDevicesCallback;
};

template <typename Interface>
struct Types;

#define D3D_CONCAT(x, y, ...) x##y

#define D3D_TYPES(Interface, ...) \
	template <> \
	struct Types<D3D_CONCAT(Interface, __VA_ARGS__)> : D3D_CONCAT(Direct3dTypes, __VA_ARGS__) \
	{}

D3D_TYPES(IDirect3D);
D3D_TYPES(IDirect3D, 2);
D3D_TYPES(IDirect3D, 3);
D3D_TYPES(IDirect3D, 7);

D3D_TYPES(IDirect3DDevice);
D3D_TYPES(IDirect3DDevice, 2);
D3D_TYPES(IDirect3DDevice, 3);
D3D_TYPES(IDirect3DDevice, 7);

#undef D3D_TYPES
#undef D3D_CONCAT
