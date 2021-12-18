#pragma once

#include <d3d.h>
#include <d3dumddi.h>

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

	D3DCOLOR colorConvert(const FormatInfo& dstFormatInfo, D3DCOLOR srcRgbaColor);
	FormatInfo getFormatInfo(D3DDDIFORMAT format);
	DDPIXELFORMAT getPixelFormat(D3DDDIFORMAT format);
}
