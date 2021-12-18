#pragma once

#include <Common/Comparison.h>

#include <ddraw.h>

inline auto toTuple(const DDPIXELFORMAT& pf)
{
	return std::make_tuple(pf.dwFlags, pf.dwFourCC, pf.dwRGBBitCount,
		pf.dwRBitMask, pf.dwGBitMask, pf.dwBBitMask, pf.dwRGBAlphaBitMask);
}
