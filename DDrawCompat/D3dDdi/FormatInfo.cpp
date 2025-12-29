#include <map>

#include <D3dDdi/FormatInfo.h>

namespace
{
	DDPIXELFORMAT getPixelFormat(const D3dDdi::FormatInfo& info);

	struct ArgbColor
	{
		BYTE blue;
		BYTE green;
		BYTE red;
		BYTE alpha;
	};

	struct FormatInfoBuilder : D3dDdi::FormatInfo
	{
		void setComponent(D3dDdi::FormatInfo::Component& component, BYTE bitCount)
		{
			component.pos = bitsPerPixel;
			component.bitCount = bitCount;
			bitsPerPixel += bitCount;
			bytesPerPixel = (bitsPerPixel + 7) / 8;
		}
	};

	struct FormatInfoXARGB : FormatInfoBuilder
	{
		FormatInfoXARGB(BYTE x, BYTE a, BYTE r, BYTE g, BYTE b)
		{
			setComponent(blue, b);
			setComponent(green, g);
			setComponent(red, r);
			setComponent(alpha, a);
			setComponent(unused, x);
			pixelFormat = getPixelFormat(*this);
		}
	};

	struct FormatInfoXABGR : FormatInfoBuilder
	{
		FormatInfoXABGR(BYTE x, BYTE a, BYTE b, BYTE g, BYTE r)
		{
			setComponent(red, r);
			setComponent(green, g);
			setComponent(blue, b);
			setComponent(alpha, a);
			setComponent(unused, x);
			pixelFormat = getPixelFormat(*this);
		}
	};

	struct FormatInfoAP : FormatInfoBuilder
	{
		FormatInfoAP(BYTE a, BYTE p)
		{
			setComponent(palette, p);
			setComponent(alpha, a);
			pixelFormat = getPixelFormat(*this);
		}
	};

	struct FormatInfoDXS : FormatInfoBuilder
	{
		FormatInfoDXS(BYTE d, BYTE x, BYTE s)
		{
			setComponent(stencil, s);
			setComponent(unused, x);
			setComponent(depth, d);
			pixelFormat = getPixelFormat(*this);
		}
	};

	struct FormatInfoXSD : FormatInfoBuilder
	{
		FormatInfoXSD(BYTE x, BYTE s, BYTE d)
		{
			setComponent(depth, d);
			setComponent(stencil, s);
			setComponent(unused, x);
			pixelFormat = getPixelFormat(*this);
		}
	};
	
	struct FormatInfoXALVU : FormatInfoBuilder
	{
		FormatInfoXALVU(BYTE x, BYTE a, BYTE l, BYTE v, BYTE u)
		{
			setComponent(du, u);
			setComponent(dv, v);
			setComponent(luminance, l);
			setComponent(alpha, a);
			setComponent(unused, x);
			pixelFormat = getPixelFormat(*this);
		}
	};

	const std::map<D3DDDIFORMAT, D3dDdi::FormatInfo> g_formatInfo = {
		{ D3DDDIFMT_R8G8B8,   FormatInfoXARGB(0, 0, 8, 8, 8) },
		{ D3DDDIFMT_A8R8G8B8, FormatInfoXARGB(0, 8, 8, 8, 8) },
		{ D3DDDIFMT_X8R8G8B8, FormatInfoXARGB(8, 0, 8, 8, 8) },
		{ D3DDDIFMT_R5G6B5,   FormatInfoXARGB(0, 0, 5, 6, 5) },
		{ D3DDDIFMT_X1R5G5B5, FormatInfoXARGB(1, 0, 5, 5, 5) },
		{ D3DDDIFMT_A1R5G5B5, FormatInfoXARGB(0, 1, 5, 5, 5) },
		{ D3DDDIFMT_A4R4G4B4, FormatInfoXARGB(0, 4, 4, 4, 4) },
		{ D3DDDIFMT_R3G3B2,   FormatInfoXARGB(0, 0, 3, 3, 2) },
		{ D3DDDIFMT_A8,       FormatInfoXARGB(0, 8, 0, 0, 0) },
		{ D3DDDIFMT_A8R3G3B2, FormatInfoXARGB(0, 8, 3, 3, 2) },
		{ D3DDDIFMT_X4R4G4B4, FormatInfoXARGB(4, 0, 4, 4, 4) },

		{ D3DDDIFMT_A2B10G10R10, FormatInfoXABGR(0, 2, 10, 10, 10) },
		{ D3DDDIFMT_A8B8G8R8,    FormatInfoXABGR(0, 8, 8, 8, 8) },
		{ D3DDDIFMT_X8B8G8R8,    FormatInfoXABGR(8, 0, 8, 8, 8) },
		{ D3DDDIFMT_A2R10G10B10, FormatInfoXARGB(0, 2, 10, 10, 10) },

		{ D3DDDIFMT_A8P8,     FormatInfoAP(8, 8) },
		{ D3DDDIFMT_P8,       FormatInfoAP(0, 8) },

		{ D3DDDIFMT_L8,       FormatInfoXALVU(0, 0, 8, 0, 0) },
		{ D3DDDIFMT_A8L8,     FormatInfoXALVU(0, 8, 8, 0, 0) },
		{ D3DDDIFMT_A4L4,     FormatInfoXALVU(0, 4, 4, 0, 0) },

		{ D3DDDIFMT_V8U8,     FormatInfoXALVU(0, 0, 0, 8, 8) },
		{ D3DDDIFMT_L6V5U5,   FormatInfoXALVU(0, 0, 6, 5, 5) },
		{ D3DDDIFMT_X8L8V8U8, FormatInfoXALVU(8, 0, 8, 8, 8) },

		{ D3DDDIFMT_D32,			FormatInfoDXS(32, 0, 0) },
		{ D3DDDIFMT_D32F_LOCKABLE,  FormatInfoDXS(32, 0, 0) },
		{ D3DDDIFMT_D32_LOCKABLE,   FormatInfoDXS(32, 0, 0) },

		{ D3DDDIFMT_D24S8,    FormatInfoDXS(24, 0, 8) },
		{ D3DDDIFMT_D24X8,    FormatInfoDXS(24, 8, 0) },
		{ D3DDDIFMT_D16,      FormatInfoDXS(16, 0, 0) },

		{ D3DDDIFMT_S8D24,     FormatInfoXSD(0, 8, 24) },
		{ D3DDDIFMT_X8D24,     FormatInfoXSD(8, 0, 24) },
		{ D3dDdi::FOURCC_DF16, FormatInfoXSD(0, 0, 16) },
		{ D3dDdi::FOURCC_DF24, FormatInfoXSD(8, 0, 24) },
		{ D3dDdi::FOURCC_INTZ, FormatInfoXSD(0, 8, 24) }
	};

	DWORD getMask(const D3dDdi::FormatInfo::Component& component)
	{
		return ((1ULL << component.bitCount) - 1) << component.pos;
	}

	DDPIXELFORMAT getPixelFormat(const D3dDdi::FormatInfo& info)
	{
		if (info.red.bitCount > 8)
		{
			return {};
		}

		DDPIXELFORMAT pf = {};
		pf.dwSize = sizeof(pf);
		pf.dwFlags =
			(info.alpha.bitCount ? (info.alpha.bitCount == info.bitsPerPixel ? DDPF_ALPHA : DDPF_ALPHAPIXELS) : 0) |
			((info.red.bitCount || info.palette.bitCount) ? DDPF_RGB : 0) |
			(info.palette.bitCount ? DDPF_PALETTEINDEXED8 : 0) |
			(info.depth.bitCount ? DDPF_ZBUFFER : 0) |
			(info.stencil.bitCount ? DDPF_STENCILBUFFER : 0) |
			(info.luminance.bitCount ? (info.du.bitCount ? DDPF_BUMPLUMINANCE : DDPF_LUMINANCE) : 0) |
			(info.du.bitCount ? DDPF_BUMPDUDV : 0);
		pf.dwRGBBitCount = info.bitsPerPixel;

		pf.dwRGBAlphaBitMask = info.alpha.bitCount == info.bitsPerPixel ? 0 : getMask(info.alpha);
		pf.dwRBitMask = getMask(info.red);
		pf.dwGBitMask = getMask(info.green);
		pf.dwBBitMask = getMask(info.blue);

		pf.dwStencilBitDepth |= info.stencil.bitCount;
		pf.dwZBitMask |= getMask(info.depth);
		pf.dwStencilBitMask |= getMask(info.stencil);

		pf.dwLuminanceBitMask |= info.du.bitCount ? 0 : getMask(info.luminance);
		pf.dwBumpDuBitMask |= getMask(info.du);
		pf.dwBumpDvBitMask |= getMask(info.dv);
		pf.dwBumpLuminanceBitMask |= info.du.bitCount ? getMask(info.luminance) : 0;
		return pf;
	}
}

namespace D3dDdi
{
	D3DCOLOR convertFrom32Bit(const FormatInfo& dstFormatInfo, D3DCOLOR srcColor)
	{
		auto& src = *reinterpret_cast<ArgbColor*>(&srcColor);

		BYTE alpha = src.alpha >> (8 - dstFormatInfo.alpha.bitCount);
		BYTE red = src.red >> (8 - dstFormatInfo.red.bitCount);
		BYTE green = src.green >> (8 - dstFormatInfo.green.bitCount);
		BYTE blue = src.blue >> (8 - (dstFormatInfo.blue.bitCount | dstFormatInfo.palette.bitCount));

		return (alpha << dstFormatInfo.alpha.pos) |
			(red << dstFormatInfo.red.pos) |
			(green << dstFormatInfo.green.pos) |
			(blue << (dstFormatInfo.blue.pos | dstFormatInfo.palette.pos));
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
		const UINT max = (1ULL << component.bitCount) - 1;
		const UINT mask = max << component.pos;
		return static_cast<float>((color & mask) >> component.pos) / max;
	}

	D3DDDIFORMAT getFormat(const DDPIXELFORMAT& pixelFormat)
	{
		if (pixelFormat.dwFlags & DDPF_FOURCC)
		{
			return static_cast<D3DDDIFORMAT>(pixelFormat.dwFourCC);
		}

		for (auto& info : g_formatInfo)
		{
			if (0 == memcmp(&info.second.pixelFormat, &pixelFormat, sizeof(pixelFormat)))
			{
				return info.first;
			}
		}

		DDPIXELFORMAT pf = g_formatInfo.at(D3DDDIFMT_X8D24).pixelFormat;
		pf.dwZBufferBitDepth = 24;   // https://bugs.winehq.org/show_bug.cgi?id=22434
		if (0 == memcmp(&pf, &pixelFormat, sizeof(pixelFormat)))
		{
			return D3DDDIFMT_X8D24;
		}

		return D3DDDIFMT_UNKNOWN;
	}

	const FormatInfo& getFormatInfo(D3DDDIFORMAT format)
	{
		static const FormatInfo empty = {};
		auto it = g_formatInfo.find(format);
		return it != g_formatInfo.end() ? it->second : empty;
	}

	const DDPIXELFORMAT& getPixelFormat(D3DDDIFORMAT format)
	{
		return getFormatInfo(format).pixelFormat;
	}
}
