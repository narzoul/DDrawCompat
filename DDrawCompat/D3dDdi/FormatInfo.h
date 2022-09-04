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
			BYTE bitCount;
			BYTE pos;
		};

		BYTE bitsPerPixel;
		BYTE bytesPerPixel;
		Component unused;
		Component alpha;
		Component red;
		Component green;
		Component blue;
		Component depth;
		Component stencil;

		FormatInfo();
		FormatInfo(BYTE unusedBitCount, BYTE alphaBitCount, BYTE redBitCount, BYTE greenBitCount, BYTE blueBitCount);
		FormatInfo(BYTE unusedBitCount, BYTE depthBitCount, BYTE stencilBitCount);
	};

	D3DCOLOR convertFrom32Bit(const FormatInfo& dstFormatInfo, D3DCOLOR srcColor);
	DeviceState::ShaderConstF convertToShaderConst(const FormatInfo& srcFormatInfo, D3DCOLOR srcColor);
	DWORD getComponent(D3DCOLOR color, const D3dDdi::FormatInfo::Component& component);
	float getComponentAsFloat(D3DCOLOR color, const D3dDdi::FormatInfo::Component& component);
	FormatInfo getFormatInfo(D3DDDIFORMAT format);
	DDPIXELFORMAT getPixelFormat(D3DDDIFORMAT format);
}
