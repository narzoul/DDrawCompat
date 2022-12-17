#pragma once

#include <d3d.h>
#include <d3dumddi.h>

#include <D3dDdi/DeviceState.h>

namespace D3dDdi
{
	static const auto FOURCC_RESZ = static_cast<D3DDDIFORMAT>(MAKEFOURCC('R', 'E', 'S', 'Z'));
	static const auto FOURCC_INTZ = static_cast<D3DDDIFORMAT>(MAKEFOURCC('I', 'N', 'T', 'Z'));
	static const auto FOURCC_NULL = static_cast<D3DDDIFORMAT>(MAKEFOURCC('N', 'U', 'L', 'L'));

	struct FormatInfo
	{
		struct Component
		{
			BYTE bitCount = 0;
			BYTE pos = 0;
		};

		BYTE bitsPerPixel = 0;
		BYTE bytesPerPixel = 0;
		Component unused;
		Component alpha;
		Component red;
		Component green;
		Component blue;
		Component palette;
		Component depth;
		Component stencil;
		Component luminance;
		Component du;
		Component dv;
		DDPIXELFORMAT pixelFormat = {};
	};

	D3DCOLOR convertFrom32Bit(const FormatInfo& dstFormatInfo, D3DCOLOR srcColor);
	DeviceState::ShaderConstF convertToShaderConst(const FormatInfo& srcFormatInfo, D3DCOLOR srcColor);
	DWORD getComponent(D3DCOLOR color, const D3dDdi::FormatInfo::Component& component);
	float getComponentAsFloat(D3DCOLOR color, const D3dDdi::FormatInfo::Component& component);
	D3DDDIFORMAT getFormat(const DDPIXELFORMAT& pixelFormat);
	FormatInfo getFormatInfo(D3DDDIFORMAT format);
	DDPIXELFORMAT getPixelFormat(D3DDDIFORMAT format);
}
