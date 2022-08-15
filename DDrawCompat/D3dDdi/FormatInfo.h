#pragma once

#include <d3d.h>
#include <d3dumddi.h>

#include <D3dDdi/DeviceState.h>

namespace D3dDdi
{
	struct FormatInfo
	{
		BYTE bitsPerPixel;
		BYTE bytesPerPixel;
		BYTE unusedBitCount;
		BYTE alphaBitCount;
		BYTE alphaPos;
		BYTE redBitCount;
		BYTE redPos;
		BYTE greenBitCount;
		BYTE greenPos;
		BYTE blueBitCount;
		BYTE bluePos;

		FormatInfo(BYTE unusedBitCount = 0, BYTE alphaBitCount = 0,
			BYTE redBitCount = 0, BYTE greenBitCount = 0, BYTE blueBitCount = 0);
	};

	D3DCOLOR convertFrom32Bit(const FormatInfo& dstFormatInfo, D3DCOLOR srcColor);
	DeviceState::ShaderConstF convertToShaderConst(const FormatInfo& srcFormatInfo, D3DCOLOR srcColor);
	FormatInfo getFormatInfo(D3DDDIFORMAT format);
	DDPIXELFORMAT getPixelFormat(D3DDDIFORMAT format);
}
