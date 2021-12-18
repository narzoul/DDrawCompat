#include <D3dDdi/FormatInfo.h>

namespace
{
	struct RgbFormatInfo : D3dDdi::FormatInfo
	{
		RgbFormatInfo(BYTE unusedBitCount, BYTE alphaBitCount, BYTE redBitCount, BYTE greenBitCount, BYTE blueBitCount)
			: FormatInfo(unusedBitCount, alphaBitCount, redBitCount, greenBitCount, blueBitCount)
		{
			redPos = greenBitCount + blueBitCount;
			greenPos = blueBitCount;
		}
	};

	struct BgrFormatInfo : D3dDdi::FormatInfo
	{
		BgrFormatInfo(BYTE unusedBitCount, BYTE alphaBitCount, BYTE blueBitCount, BYTE greenBitCount, BYTE redBitCount)
			: FormatInfo(unusedBitCount, alphaBitCount, redBitCount, greenBitCount, blueBitCount)
		{
			greenPos = redBitCount;
			bluePos = redBitCount + greenBitCount;
		}
	};
}

namespace D3dDdi
{
	FormatInfo::FormatInfo(BYTE unusedBitCount, BYTE alphaBitCount, BYTE redBitCount, BYTE greenBitCount, BYTE blueBitCount)
		: bitsPerPixel(unusedBitCount + alphaBitCount + redBitCount + greenBitCount + blueBitCount)
		, bytesPerPixel((bitsPerPixel + 7) / 8)
		, unusedBitCount(unusedBitCount)
		, alphaBitCount(alphaBitCount)
		, alphaPos(redBitCount + greenBitCount + blueBitCount)
		, redBitCount(redBitCount)
		, redPos(0)
		, greenBitCount(greenBitCount)
		, greenPos(0)
		, blueBitCount(blueBitCount)
		, bluePos(0)
	{
	}

	D3DCOLOR colorConvert(const FormatInfo& dstFormatInfo, D3DCOLOR srcRgbaColor)
	{
		struct ArgbColor
		{
			BYTE blue;
			BYTE green;
			BYTE red;
			BYTE alpha;
		};
		
		auto& srcColor = *reinterpret_cast<ArgbColor*>(&srcRgbaColor);

		BYTE alpha = srcColor.alpha >> (8 - dstFormatInfo.alphaBitCount);
		BYTE red = srcColor.red >> (8 - dstFormatInfo.redBitCount);
		BYTE green = srcColor.green >> (8 - dstFormatInfo.greenBitCount);
		BYTE blue = srcColor.blue >> (8 - dstFormatInfo.blueBitCount);

		return (alpha << dstFormatInfo.alphaPos) |
			(red << dstFormatInfo.redPos) |
			(green << dstFormatInfo.greenPos) |
			(blue << dstFormatInfo.bluePos);
	}

	FormatInfo getFormatInfo(D3DDDIFORMAT format)
	{
		switch (format)
		{
		case D3DDDIFMT_R3G3B2:		return RgbFormatInfo(0, 0, 3, 3, 2);
		case D3DDDIFMT_A8:			return RgbFormatInfo(0, 8, 0, 0, 0);
		case D3DDDIFMT_P8:			return RgbFormatInfo(0, 0, 0, 0, 8);
		case D3DDDIFMT_R8:			return RgbFormatInfo(0, 0, 8, 0, 0);

		case D3DDDIFMT_R5G6B5:		return RgbFormatInfo(0, 0, 5, 6, 5);
		case D3DDDIFMT_X1R5G5B5:	return RgbFormatInfo(1, 0, 5, 5, 5);
		case D3DDDIFMT_A1R5G5B5:	return RgbFormatInfo(0, 1, 5, 5, 5);
		case D3DDDIFMT_A4R4G4B4:	return RgbFormatInfo(0, 4, 4, 4, 4);
		case D3DDDIFMT_A8R3G3B2:	return RgbFormatInfo(0, 8, 3, 3, 2);
		case D3DDDIFMT_X4R4G4B4:	return RgbFormatInfo(4, 0, 4, 4, 4);
		case D3DDDIFMT_A8P8:		return RgbFormatInfo(0, 8, 0, 0, 8);
		case D3DDDIFMT_G8R8:		return BgrFormatInfo(0, 0, 0, 8, 8);

		case D3DDDIFMT_R8G8B8:		return RgbFormatInfo(0, 0, 8, 8, 8);

		case D3DDDIFMT_A8R8G8B8:	return RgbFormatInfo(0, 8, 8, 8, 8);
		case D3DDDIFMT_X8R8G8B8:	return RgbFormatInfo(8, 0, 8, 8, 8);
		case D3DDDIFMT_A8B8G8R8:	return BgrFormatInfo(0, 8, 8, 8, 8);
		case D3DDDIFMT_X8B8G8R8:	return BgrFormatInfo(8, 0, 8, 8, 8);

		default:
			return FormatInfo();
		}
	}

	DDPIXELFORMAT getPixelFormat(D3DDDIFORMAT format)
	{
		auto info = getFormatInfo(format);
		if (0 == info.bytesPerPixel)
		{
			return {};
		}

		DDPIXELFORMAT pf = {};
		pf.dwSize = sizeof(pf);
		pf.dwRGBBitCount = info.bitsPerPixel;
		if (0 != pf.dwRGBBitCount)
		{
			pf.dwFlags = DDPF_RGB;
			pf.dwRBitMask = (0xFF >> (8 - info.redBitCount)) << info.redPos;
			pf.dwGBitMask = (0xFF >> (8 - info.greenBitCount)) << info.greenPos;
			pf.dwBBitMask = (0xFF >> (8 - info.blueBitCount)) << info.bluePos;
		}
		if (0 != info.alphaBitCount)
		{
			pf.dwFlags |= (0 == pf.dwFlags) ? DDPF_ALPHA : DDPF_ALPHAPIXELS;
			pf.dwRGBAlphaBitMask = (0xFF >> (8 - info.alphaBitCount)) << info.alphaPos;
		}
		return pf;
	}
}
