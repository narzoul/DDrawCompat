#include "D3dDdi/DeviceFuncs.h"

std::ostream& operator<<(std::ostream& os, const D3DDDI_RATIONAL& val)
{
	return Compat::LogStruct(os)
		<< val.Numerator
		<< val.Denominator;
}

std::ostream& operator<<(std::ostream& os, const D3DDDI_SURFACEINFO& val)
{
	return Compat::LogStruct(os)
		<< val.Width
		<< val.Height
		<< val.Depth
		<< val.pSysMem
		<< val.SysMemPitch
		<< val.SysMemSlicePitch;
}

std::ostream& operator<<(std::ostream& os, const D3DDDIARG_CREATERESOURCE& val)
{
	return Compat::LogStruct(os)
		<< val.Format
		<< val.Pool
		<< val.MultisampleType
		<< val.MultisampleQuality
		<< Compat::array(val.pSurfList, val.SurfCount)
		<< val.SurfCount
		<< val.MipLevels
		<< val.Fvf
		<< val.VidPnSourceId
		<< val.RefreshRate
		<< val.hResource
		<< Compat::hex(val.Flags.Value)
		<< val.Rotation;
}

std::ostream& operator<<(std::ostream& os, const D3DDDIARG_CREATERESOURCE2& val)
{
	return Compat::LogStruct(os)
		<< val.Format
		<< val.Pool
		<< val.MultisampleType
		<< val.MultisampleQuality
		<< Compat::array(val.pSurfList, val.SurfCount)
		<< val.SurfCount
		<< val.MipLevels
		<< val.Fvf
		<< val.VidPnSourceId
		<< val.RefreshRate
		<< val.hResource
		<< Compat::hex(val.Flags.Value)
		<< val.Rotation
		<< Compat::hex(val.Flags2.Value);
}

std::ostream& operator<<(std::ostream& os, const D3DDDIARG_LOCK& val)
{
	return Compat::LogStruct(os)
		<< val.hResource
		<< val.SubResourceIndex
		<< val.Box
		<< val.pSurfData
		<< val.Pitch
		<< val.SlicePitch
		<< Compat::hex(val.Flags.Value);
}

std::ostream& operator<<(std::ostream& os, const D3DDDIARG_OPENRESOURCE& val)
{
	return Compat::LogStruct(os)
		<< val.NumAllocations
		<< Compat::array(val.pOpenAllocationInfo, val.NumAllocations)
		<< Compat::hex(val.hKMResource)
		<< val.pPrivateDriverData
		<< val.PrivateDriverDataSize
		<< val.hResource
		<< val.Rotation
		<< Compat::hex(val.Flags.Value);
}

std::ostream& operator<<(std::ostream& os, const D3DDDIARG_UNLOCK& val)
{
	return Compat::LogStruct(os)
		<< val.hResource
		<< val.SubResourceIndex
		<< Compat::hex(val.Flags.Value);
}

std::ostream& operator<<(std::ostream& os, const D3DDDIBOX& box)
{
	return Compat::LogStruct(os)
		<< box.Left
		<< box.Top
		<< box.Right
		<< box.Bottom
		<< box.Front
		<< box.Back;
}

namespace
{
	HRESULT APIENTRY destroyDevice(HANDLE hDevice)
	{
		HRESULT result = D3dDdi::DeviceFuncs::s_origVtables.at(hDevice).pfnDestroyDevice(hDevice);
		if (SUCCEEDED(result))
		{
			D3dDdi::DeviceFuncs::s_origVtables.erase(hDevice);
		}
		return result;
	}
}

namespace D3dDdi
{
	void DeviceFuncs::setCompatVtable(D3DDDI_DEVICEFUNCS& vtable)
	{
		vtable.pfnDestroyDevice = &destroyDevice;
	}
}
