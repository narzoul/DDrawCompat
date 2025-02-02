#pragma once

#define VISIT_DDCAPS(visit) \
	visit(dwSize) \
	visit(dwCaps) \
	visit(dwCaps2) \
	visit(dwCKeyCaps) \
	visit(dwFXCaps) \
	visit(dwFXAlphaCaps) \
	visit(dwPalCaps) \
	visit(dwSVCaps) \
	visit(dwAlphaBltConstBitDepths) \
	visit(dwAlphaBltPixelBitDepths) \
	visit(dwAlphaBltSurfaceBitDepths) \
	visit(dwAlphaOverlayConstBitDepths) \
	visit(dwAlphaOverlayPixelBitDepths) \
	visit(dwAlphaOverlaySurfaceBitDepths) \
	visit(dwZBufferBitDepths) \
	visit(dwVidMemTotal) \
	visit(dwVidMemFree) \
	visit(dwMaxVisibleOverlays) \
	visit(dwCurrVisibleOverlays) \
	visit(dwNumFourCCCodes) \
	visit(dwAlignBoundarySrc) \
	visit(dwAlignSizeSrc) \
	visit(dwAlignBoundaryDest) \
	visit(dwAlignSizeDest) \
	visit(dwAlignStrideAlign) \
	visit(dwRops) \
	visit(ddsOldCaps) \
	visit(dwMinOverlayStretch) \
	visit(dwMaxOverlayStretch) \
	visit(dwMinLiveVideoStretch) \
	visit(dwMaxLiveVideoStretch) \
	visit(dwMinHwCodecStretch) \
	visit(dwMaxHwCodecStretch) \
	visit(dwReserved1) \
	visit(dwReserved2) \
	visit(dwReserved3) \
	visit(dwSVBCaps) \
	visit(dwSVBCKeyCaps) \
	visit(dwSVBFXCaps) \
	visit(dwSVBRops) \
	visit(dwVSBCaps) \
	visit(dwVSBCKeyCaps) \
	visit(dwVSBFXCaps) \
	visit(dwVSBRops) \
	visit(dwSSBCaps) \
	visit(dwSSBCKeyCaps) \
	visit(dwSSBFXCaps) \
	visit(dwSSBRops) \
	visit(dwMaxVideoPorts) \
	visit(dwCurrVideoPorts) \
	visit(dwSVBCaps2) \
	visit(dwNLVBCaps) \
	visit(dwNLVBCaps2) \
	visit(dwNLVBCKeyCaps) \
	visit(dwNLVBFXCaps) \
	visit(dwNLVBRops) \
	visit(ddsCaps)

#define VISIT_DDSCAPS(visit) \
	visit(dwCaps)

#define VISIT_DDSCAPS2(visit) \
	visit(dwCaps) \
	visit(dwCaps2) \
	visit(dwCaps3) \
	visit(dwCaps4)
