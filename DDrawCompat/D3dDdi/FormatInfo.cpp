#include <D3dDdi/FormatInfo.h>

namespace
{
	struct ArgbColor
	{
		BYTE blue;
		BYTE green;
		BYTE red;
		BYTE alpha;
	};

	struct RgbFormatInfo : D3dDdi::FormatInfo
	{
		RgbFormatInfo(BYTE unusedBitCount, BYTE alphaBitCount, BYTE redBitCount, BYTE greenBitCount, BYTE blueBitCount)
			: FormatInfo(unusedBitCount, alphaBitCount, redBitCount, greenBitCount, blueBitCount)
		{
			red.pos = greenBitCount + blueBitCount;
			green.pos = blueBitCount;
		}
	};

	struct BgrFormatInfo : D3dDdi::FormatInfo
	{
		BgrFormatInfo(BYTE unusedBitCount, BYTE alphaBitCount, BYTE blueBitCount, BYTE greenBitCount, BYTE redBitCount)
			: FormatInfo(unusedBitCount, alphaBitCount, redBitCount, greenBitCount, blueBitCount)
		{
			green.pos = redBitCount;
			blue.pos = redBitCount + greenBitCount;
		}
	};

	struct DxsFormatInfo : D3dDdi::FormatInfo
	{
		DxsFormatInfo(BYTE depthBitCount, BYTE unusedBitCount, BYTE stencilBitCount)
			: FormatInfo(unusedBitCount, depthBitCount, stencilBitCount)
		{
			depth.pos = unusedBitCount + stencilBitCount;
			unused.pos = stencilBitCount;
		}
	};

	struct XsdFormatInfo : D3dDdi::FormatInfo
	{
		XsdFormatInfo(BYTE unusedBitCount, BYTE stencilBitCount, BYTE depthBitCount)
			: FormatInfo(unusedBitCount, depthBitCount, stencilBitCount)
		{
			unused.pos = stencilBitCount + depthBitCount;
			stencil.pos = depthBitCount;
		}
	};

	DWORD getMask(const D3dDdi::FormatInfo::Component& component)
	{
		return ((1 << component.bitCount) - 1) << component.pos;
	}
}

namespace D3dDdi
{
	FormatInfo::FormatInfo()
	{
		memset(this, 0, sizeof(*this));
	}

	FormatInfo::FormatInfo(BYTE unusedBitCount, BYTE alphaBitCount, BYTE redBitCount, BYTE greenBitCount, BYTE blueBitCount)
		: bitsPerPixel(unusedBitCount + alphaBitCount + redBitCount + greenBitCount + blueBitCount)
		, bytesPerPixel((bitsPerPixel + 7) / 8)
		, unused{ unusedBitCount, static_cast<BYTE>(alphaBitCount + redBitCount + greenBitCount + blueBitCount) }
		, alpha{ alphaBitCount, static_cast<BYTE>(redBitCount + greenBitCount + blueBitCount) }
		, red{ redBitCount, 0 }
		, green{ greenBitCount, 0 }
		, blue{ blueBitCount, 0 }
		, depth{}
		, stencil{}
	{
	}

	FormatInfo::FormatInfo(BYTE unusedBitCount, BYTE depthBitCount, BYTE stencilBitCount)
		: bitsPerPixel(unusedBitCount + depthBitCount + stencilBitCount)
		, bytesPerPixel((bitsPerPixel + 7) / 8)
		, unused{ unusedBitCount, 0 }
		, alpha{}
		, red{}
		, green{}
		, blue{}
		, depth{ depthBitCount, 0 }
		, stencil{ stencilBitCount, 0 }
	{
	}

	D3DCOLOR convertFrom32Bit(const FormatInfo& dstFormatInfo, D3DCOLOR srcColor)
	{
		auto& src = *reinterpret_cast<ArgbColor*>(&srcColor);

		BYTE alpha = src.alpha >> (8 - dstFormatInfo.alpha.bitCount);
		BYTE red = src.red >> (8 - dstFormatInfo.red.bitCount);
		BYTE green = src.green >> (8 - dstFormatInfo.green.bitCount);
		BYTE blue = src.blue >> (8 - dstFormatInfo.blue.bitCount);

		return (alpha << dstFormatInfo.alpha.pos) |
			(red << dstFormatInfo.red.pos) |
			(green << dstFormatInfo.green.pos) |
			(blue << dstFormatInfo.blue.pos);
	}

	DeviceState::ShaderConstF convertToShaderConst(const FormatInfo& srcFormatInfo, D3DCOLOR srcColor)
	{
		return {
			getComponentAsFloat(srcColor, srcFormatInfo.red),
			getComponentAsFloat(srcColor, srcFormatInfo.green),
			getComponentAsFloat(srcColor, srcFormatInfo.blue),
			getComponentAsFloat(srcColor, srcFormatInfo.alpha)
		};
	}

	DWORD getComponent(D3DCOLOR color, const D3dDdi::FormatInfo::Component& component)
	{
		return (color & getMask(component)) >> component.pos;
	}

	float getComponentAsFloat(D3DCOLOR color, const D3dDdi::FormatInfo::Component& component)
	{
		if (0 == component.bitCount)
		{
			return 0;
		}
		const UINT max = (1 << component.bitCount) - 1;
		const UINT mask = max << component.pos;
		return static_cast<float>((color & mask) >> component.pos) / max;
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

		case D3DDDIFMT_D32:			return DxsFormatInfo(32, 0, 0);
		case D3DDDIFMT_D15S1:		return DxsFormatInfo(15, 0, 1);
		case D3DDDIFMT_D24S8:		return DxsFormatInfo(24, 0, 8);
		case D3DDDIFMT_D24X8:		return DxsFormatInfo(24, 8, 0);
		case D3DDDIFMT_D24X4S4:		return DxsFormatInfo(24, 4, 4);
		case D3DDDIFMT_D16:			return DxsFormatInfo(16, 0, 0);
		case D3DDDIFMT_S1D15:		return XsdFormatInfo(0, 1, 15);
		case D3DDDIFMT_S8D24:		return XsdFormatInfo(0, 8, 24);
		case D3DDDIFMT_X8D24:		return XsdFormatInfo(8, 0, 24);
		case D3DDDIFMT_X4S4D24:		return XsdFormatInfo(4, 4, 24);

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
		if (0 != info.depth.bitCount)
		{
			pf.dwFlags = DDPF_ZBUFFER;
			pf.dwZBufferBitDepth = info.depth.bitCount;
			pf.dwZBitMask = getMask(info.depth);
			if (0 != info.stencil.bitCount)
			{
				pf.dwFlags |= DDPF_STENCILBUFFER;
				pf.dwZBufferBitDepth += info.stencil.bitCount;
				pf.dwStencilBitDepth = info.stencil.bitCount;
				pf.dwStencilBitMask = getMask(info.stencil);
			}
		}
		else
		{
			pf.dwRGBBitCount = info.bitsPerPixel;
			if (info.bitsPerPixel > info.alpha.bitCount)
			{
				pf.dwFlags = DDPF_RGB;
				pf.dwRBitMask = getMask(info.red);;
				pf.dwGBitMask = getMask(info.green);
				pf.dwBBitMask = getMask(info.blue);
			}
			if (0 != info.alpha.bitCount)
			{
				pf.dwFlags |= (0 == pf.dwFlags) ? DDPF_ALPHA : DDPF_ALPHAPIXELS;
				pf.dwRGBAlphaBitMask = getMask(info.alpha);
			}
		}
		return pf;
	}
}
