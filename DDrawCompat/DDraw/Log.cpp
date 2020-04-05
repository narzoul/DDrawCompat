#include <d3d.h>

#include <Common/Log.h>
#include <DDraw/Log.h>

std::ostream& operator<<(std::ostream& os, const DDSCAPS& caps)
{
	return Compat::LogStruct(os)
		<< Compat::hex(caps.dwCaps);
}

std::ostream& operator<<(std::ostream& os, const DDSCAPS2& caps)
{
	return Compat::LogStruct(os)
		<< Compat::hex(caps.dwCaps)
		<< Compat::hex(caps.dwCaps2)
		<< Compat::hex(caps.dwCaps3)
		<< Compat::hex(caps.dwCaps4);
}

std::ostream& operator<<(std::ostream& os, const DDPIXELFORMAT& pf)
{
	return Compat::LogStruct(os)
		<< Compat::hex(pf.dwFlags)
		<< Compat::hex(pf.dwFourCC)
		<< pf.dwRGBBitCount
		<< Compat::hex(pf.dwRBitMask)
		<< Compat::hex(pf.dwGBitMask)
		<< Compat::hex(pf.dwBBitMask)
		<< Compat::hex(pf.dwRGBAlphaBitMask);
}

std::ostream& operator<<(std::ostream& os, const DDSURFACEDESC& sd)
{
	DDSURFACEDESC2 sd2 = {};
	memcpy(&sd2, &sd, sizeof(sd));
	return os << sd2;
}

std::ostream& operator<<(std::ostream& os, const DDSURFACEDESC2& sd)
{
	return Compat::LogStruct(os)
		<< Compat::hex(sd.dwFlags)
		<< sd.dwHeight
		<< sd.dwWidth
		<< sd.lPitch
		<< sd.dwBackBufferCount
		<< sd.dwMipMapCount
		<< sd.dwAlphaBitDepth
		<< sd.dwReserved
		<< sd.lpSurface
		<< sd.ddpfPixelFormat
		<< sd.ddsCaps
		<< sd.dwTextureStage;
}

std::ostream& operator<<(std::ostream& os, const GUID& guid)
{
#define LOG_GUID(g) if (g == guid) return os << #g
	LOG_GUID(CLSID_DirectDraw);
	LOG_GUID(CLSID_DirectDraw7);
	LOG_GUID(CLSID_DirectDrawClipper);
	LOG_GUID(IID_IDirectDraw);
	LOG_GUID(IID_IDirectDraw2);
	LOG_GUID(IID_IDirectDraw4);
	LOG_GUID(IID_IDirectDraw7);
	LOG_GUID(IID_IDirectDrawSurface);
	LOG_GUID(IID_IDirectDrawSurface2);
	LOG_GUID(IID_IDirectDrawSurface3);
	LOG_GUID(IID_IDirectDrawSurface4);
	LOG_GUID(IID_IDirectDrawSurface7);
	LOG_GUID(IID_IDirectDrawPalette);
	LOG_GUID(IID_IDirectDrawClipper);
	LOG_GUID(IID_IDirectDrawColorControl);
	LOG_GUID(IID_IDirectDrawGammaControl);
	LOG_GUID(IID_IDirect3D);
	LOG_GUID(IID_IDirect3D2);
	LOG_GUID(IID_IDirect3D3);
	LOG_GUID(IID_IDirect3D7);
	LOG_GUID(IID_IDirect3DRampDevice);
	LOG_GUID(IID_IDirect3DRGBDevice);
	LOG_GUID(IID_IDirect3DHALDevice);
	LOG_GUID(IID_IDirect3DMMXDevice);
	LOG_GUID(IID_IDirect3DRefDevice);
	LOG_GUID(IID_IDirect3DNullDevice);
	LOG_GUID(IID_IDirect3DTnLHalDevice);
	LOG_GUID(IID_IDirect3DDevice);
	LOG_GUID(IID_IDirect3DDevice2);
	LOG_GUID(IID_IDirect3DDevice3);
	LOG_GUID(IID_IDirect3DDevice7);
	LOG_GUID(IID_IDirect3DTexture);
	LOG_GUID(IID_IDirect3DTexture2);
	LOG_GUID(IID_IDirect3DLight);
	LOG_GUID(IID_IDirect3DMaterial);
	LOG_GUID(IID_IDirect3DMaterial2);
	LOG_GUID(IID_IDirect3DMaterial3);
	LOG_GUID(IID_IDirect3DExecuteBuffer);
	LOG_GUID(IID_IDirect3DViewport);
	LOG_GUID(IID_IDirect3DViewport2);
	LOG_GUID(IID_IDirect3DViewport3);
	LOG_GUID(IID_IDirect3DVertexBuffer);
	LOG_GUID(IID_IDirect3DVertexBuffer7);
#undef LOG_GUID

	OLECHAR str[256] = {};
	StringFromGUID2(guid, str, sizeof(str));
	return os << str;
}
