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
