#pragma once

#include <array>
#include <memory>

#include <Windows.h>

#include <Common/Vector.h>
#include <D3dDdi/DeviceState.h>
#include <D3dDdi/MetaShader.h>
#include <D3dDdi/ResourceDeleter.h>
#include <Gdi/Region.h>

struct RectF;

namespace D3dDdi
{
	class Device;
	class Resource;

	class ShaderBlitter
	{
	public:
		struct ColorKeyInfo
		{
			UINT colorKey;
			D3DDDIFORMAT format;
		};

		ShaderBlitter(Device& device);
		ShaderBlitter(const ShaderBlitter&) = delete;
		ShaderBlitter(ShaderBlitter&&) = delete;
		ShaderBlitter& operator=(const ShaderBlitter&) = delete;
		ShaderBlitter& operator=(ShaderBlitter&&) = delete;

		void alphaBlendBlt(const Resource& dstResource, UINT dstSubResourceIndex, const RECT& dstRect,
			const Resource& srcResource, UINT srcSubResourceIndex, const RECT& srcRect,
			ColorKeyInfo srcColorKey, BYTE alpha, const Gdi::Region& srcRgn);
		void bilinearBlt(const Resource& dstResource, UINT dstSubResourceIndex, const RECT& dstRect,
			const Resource& srcResource, UINT srcSubResourceIndex, const RECT& srcRect, UINT blurPercent);
		void bicubicBlt(const Resource& dstResource, UINT dstSubResourceIndex, const RECT& dstRect,
			const Resource& srcResource, UINT srcSubResourceIndex, const RECT& srcRect, UINT blurPercent);
		void colorKeyBlt(const Resource& dstResource, UINT dstSubResourceIndex,
			const Resource& srcResource, UINT srcSubResourceIndex, ColorKeyInfo srcColorKey);
		void cursorBlt(const Resource& dstResource, UINT dstSubResourceIndex, const RECT& dstRect,
			HCURSOR cursor, POINT pt);
		void depthCopy(const Resource& dstResource, UINT dstSubResourceIndex, const RECT& dstRect,
			const Resource& srcResource, UINT srcSubResourceIndex, const RECT& srcRect);
		void depthLockRefBlt(const Resource& dstResource, UINT dstSubResourceIndex, const RECT& dstRect,
			const Resource& srcResource, UINT srcSubResourceIndex, const RECT& srcRect,
			const Resource& lockRefResource);
		void depthRead(const Resource& dstResource, UINT dstSubResourceIndex, const RECT& dstRect,
			const Resource& srcResource, UINT srcSubResourceIndex, const RECT& srcRect);
		void depthWrite(const Resource& dstResource, UINT dstSubResourceIndex, const RECT& dstRect,
			const Resource& srcResource, UINT srcSubResourceIndex, const RECT& srcRect);
		void displayBlt(Resource& dstResource, UINT dstSubResourceIndex, const RECT& dstRect,
			const Resource& srcResource, UINT srcSubResourceIndex, const RECT& srcRect);
		void lanczosBlt(const Resource& dstResource, UINT dstSubResourceIndex, const RECT& dstRect,
			const Resource& srcResource, UINT srcSubResourceIndex, const RECT& srcRect, UINT lobes);
		void lockRefBlt(const Resource& dstResource, UINT dstSubResourceIndex, const RECT& dstRect,
			const Resource& srcResource, UINT srcSubResourceIndex, const RECT& srcRect,
			const Resource& lockRefResource);
		void palettizedBlt(const Resource& dstResource, UINT dstSubResourceIndex, const RECT& dstRect,
			const Resource& srcResource, UINT srcSubResourceIndex, const RECT& srcRect, RGBQUAD palette[256]);
		void pointBlt(const Resource& dstResource, UINT dstSubResourceIndex, const RECT& dstRect,
			const Resource& srcResource, UINT srcSubResourceIndex, const RECT& srcRect);
		void splineBlt(const Resource& dstResource, UINT dstSubResourceIndex, const RECT& dstRect,
			const Resource& srcResource, UINT srcSubResourceIndex, const RECT& srcRect, UINT lobes);
		void textureBlt(const Resource& dstResource, UINT dstSubResourceIndex, const RECT& dstRect,
			const Resource& srcResource, UINT srcSubResourceIndex, const RECT& srcRect,
			UINT filter, ColorKeyInfo srcColorKey = {}, const BYTE* alpha = nullptr,
			const Gdi::Region& srcRgn = nullptr);

		MetaShader& getMetaShader() { return m_metaShader; }

		static void resetGammaRamp();
		static void setGammaRamp(const D3DDDI_GAMMA_RAMP_RGB256x3x16& ramp);

	private:
		struct ConvolutionParams
		{
			Float2 textureSize;
			Float2 sampleCoordOffset;
			Float2 textureCoordOffset[2];
			Float2 kernelCoordOffset[2];
			Float2 textureCoordStep;
			Float2 kernelCoordStep;
			Float2 textureCoordStepPri;
			Float2 textureCoordStepSec;
			Float2 kernelCoordStepPri;
			float support;
			float supportRcp;
			DeviceState::ShaderConstF maxRgb;
			DeviceState::ShaderConstF maxRgbRcp;
			float ditherScale;
			float ditherOffset;
			Float2 padding;
			alignas(sizeof(DeviceState::ShaderConstF)) std::array<DeviceState::ShaderConstF, 4> extra;
		};

		struct Vertex
		{
			std::array<float, 2> xy;
			float z;
			float rhw;
			std::array<float, 2> tc[4];
		};

		void blt(const Resource& dstResource, UINT dstSubResourceIndex, const RECT& dstRect,
			const Resource& srcResource, UINT srcSubResourceIndex, const RECT& srcRect, const DeviceState::TempShader& ps,
			UINT filter, UINT flags = 0, const BYTE* alpha = nullptr, const Gdi::Region& srcRgn = nullptr);
		void convolution(const Resource& dstResource, UINT dstSubResourceIndex, const RECT& dstRect,
			const Resource& srcResource, UINT srcSubResourceIndex, const RECT& srcRect,
			Float2 support, const DeviceState::TempShader& ps, const std::function<void(bool)> setExtraParams, DWORD flags);
		void convolutionBlt(const Resource& dstResource, UINT dstSubResourceIndex, const RECT& dstRect,
			const Resource& srcResource, UINT srcSubResourceIndex, const RECT& srcRect,
			Float2 support, const DeviceState::TempShader& ps, const std::function<void(bool)> setExtraParams = {});
		void depthWrite(const Resource& dstResource, UINT dstSubResourceIndex, const RECT& dstRect,
			const Resource& srcResource, UINT srcSubResourceIndex, const RECT& srcRect, const DeviceState::TempShader& ps);

		template <int N>
		DeviceState::TempShader createPixelShader(const BYTE(&code)[N])
		{
			return createPixelShader(code, N);
		}

		DeviceState::TempShader createPixelShader(const BYTE* code, UINT size);
		std::unique_ptr<void, ResourceDeleter> createVertexShaderDecl();
		void drawRect(const RectF& rect);
		void setTempTextureStage(UINT stage, const Resource& texture, UINT subResourceIndex,
			const RECT& rect, UINT filter, UINT textureAddress = D3DTADDRESS_CLAMP);
		void setTextureCoords(UINT stage, const RECT& rect, UINT width, UINT height);

		Device& m_device;
		MetaShader m_metaShader;
		DeviceState::TempShader m_psAlphaBlend;
		DeviceState::TempShader m_psBilinear;
		DeviceState::TempShader m_psColorKey;
		DeviceState::TempShader m_psColorKeyBlend;
		DeviceState::TempShader m_psCubicConvolution[3];
		DeviceState::TempShader m_psDepthCopy;
		DeviceState::TempShader m_psDepthCopyPcf16;
		DeviceState::TempShader m_psDepthCopyPcf24;
		DeviceState::TempShader m_psDepthLockRef16;
		DeviceState::TempShader m_psDepthLockRef24;
		DeviceState::TempShader m_psDepthRead16;
		DeviceState::TempShader m_psDepthRead24;
		DeviceState::TempShader m_psDepthReadPcf16;
		DeviceState::TempShader m_psDepthReadPcf24;
		DeviceState::TempShader m_psDepthWrite16;
		DeviceState::TempShader m_psDepthWrite24;
		DeviceState::TempShader m_psDitheredGammaControl;
		DeviceState::TempShader m_psDrawCursor;
		DeviceState::TempShader m_psLanczos;
		DeviceState::TempShader m_psLockRef;
		DeviceState::TempShader m_psPaletteLookup;
		DeviceState::TempShader m_psPoint;
		DeviceState::TempShader m_psPointNoFilter;
		DeviceState::TempShader m_psTextureSampler;
		std::unique_ptr<void, ResourceDeleter> m_vertexShaderDecl;
		ConvolutionParams m_convolutionParams;
		std::array<Vertex, 4> m_vertices;
	};

	std::ostream& operator<<(std::ostream& os, ShaderBlitter::ColorKeyInfo ck);
}
