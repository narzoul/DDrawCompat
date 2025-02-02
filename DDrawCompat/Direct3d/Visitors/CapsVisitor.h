#pragma once

#define VISIT_D3DDEVICEDESC(visit) \
	visit(dwSize) \
	visit(dwFlags) \
	visit(dcmColorModel) \
	visit(dwDevCaps) \
	visit(dtcTransformCaps) \
	visit(bClipping) \
	visit(dlcLightingCaps) \
	visit(dpcLineCaps) \
	visit(dpcTriCaps) \
	visit(dwDeviceRenderBitDepth) \
	visit(dwDeviceZBufferBitDepth) \
	visit(dwMaxBufferSize) \
	visit(dwMaxVertexCount) \
	visit(dwMinTextureWidth) \
	visit(dwMinTextureHeight) \
	visit(dwMaxTextureWidth) \
	visit(dwMaxTextureHeight) \
	visit(dwMinStippleWidth) \
	visit(dwMaxStippleWidth) \
	visit(dwMinStippleHeight) \
	visit(dwMaxStippleHeight) \
	visit(dwMaxTextureRepeat) \
	visit(dwMaxTextureAspectRatio) \
	visit(dwMaxAnisotropy) \
	visit(dvGuardBandLeft) \
	visit(dvGuardBandTop) \
	visit(dvGuardBandRight) \
	visit(dvGuardBandBottom) \
	visit(dvExtentsAdjust) \
	visit(dwStencilCaps) \
	visit(dwFVFCaps) \
	visit(dwTextureOpCaps) \
	visit(wMaxTextureBlendStages) \
	visit(wMaxSimultaneousTextures)

#define VISIT_D3DDEVICEDESC7(visit) \
	visit(dwDevCaps) \
	visit(dpcLineCaps) \
	visit(dpcTriCaps) \
	visit(dwDeviceRenderBitDepth) \
	visit(dwDeviceZBufferBitDepth) \
	visit(dwMinTextureWidth) \
	visit(dwMinTextureHeight) \
	visit(dwMaxTextureWidth) \
	visit(dwMaxTextureHeight) \
	visit(dwMaxTextureRepeat) \
	visit(dwMaxTextureAspectRatio) \
	visit(dwMaxAnisotropy) \
	visit(dvGuardBandLeft) \
	visit(dvGuardBandTop) \
	visit(dvGuardBandRight) \
	visit(dvGuardBandBottom) \
	visit(dvExtentsAdjust) \
	visit(dwStencilCaps) \
	visit(dwFVFCaps) \
	visit(dwTextureOpCaps) \
	visit(wMaxTextureBlendStages) \
	visit(wMaxSimultaneousTextures) \
	visit(dwMaxActiveLights) \
	visit(dvMaxVertexW) \
	visit(deviceGUID) \
	visit(wMaxUserClipPlanes) \
	visit(wMaxVertexBlendMatrices) \
	visit(dwVertexProcessingCaps) \
	visit(dwReserved1) \
	visit(dwReserved2) \
	visit(dwReserved3) \
	visit(dwReserved4)

#define VISIT_D3DLIGHTINGCAPS(visit) \
	visit(dwSize) \
	visit(dwCaps) \
	visit(dwLightingModel) \
	visit(dwNumLights)

#define VISIT_D3DPRIMCAPS(visit) \
	visit(dwSize) \
	visit(dwMiscCaps) \
	visit(dwRasterCaps) \
	visit(dwZCmpCaps) \
	visit(dwSrcBlendCaps) \
	visit(dwDestBlendCaps) \
	visit(dwAlphaCmpCaps) \
	visit(dwShadeCaps) \
	visit(dwTextureCaps) \
	visit(dwTextureFilterCaps) \
	visit(dwTextureBlendCaps) \
	visit(dwTextureAddressCaps) \
	visit(dwStippleWidth) \
	visit(dwStippleHeight) \

#define VISIT_D3DTRANSFORMCAPS(visit) \
	visit(dwSize) \
	visit(dwCaps)
