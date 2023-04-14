#pragma once

#include <array>
#include <memory>

#include <Windows.h>

#include <Common/Vector.h>
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
		ShaderBlitter(Device& device);
		ShaderBlitter(const ShaderBlitter&) = delete;
		ShaderBlitter(ShaderBlitter&&) = delete;
		ShaderBlitter& operator=(const ShaderBlitter&) = delete;
		ShaderBlitter& operator=(ShaderBlitter&&) = delete;

		void bilinearBlt(const Resource& dstResource, UINT dstSubResourceIndex, const RECT& dstRect,
			const Resource& srcResource, UINT srcSubResourceIndex, const RECT& srcRect, UINT blurPercent);
		void bicubicBlt(const Resource& dstResource, UINT dstSubResourceIndex, const RECT& dstRect,
			const Resource& srcResource, UINT srcSubResourceIndex, const RECT& srcRect, UINT blurPercent);
		void colorKeyBlt(const Resource& dstResource, UINT dstSubResourceIndex,
			const Resource& srcResource, UINT srcSubResourceIndex, DeviceState::ShaderConstF srcColorKey);
		void cursorBlt(const Resource& dstResource, UINT dstSubResourceIndex, const RECT& dstRect,
			HCURSOR cursor, POINT pt);
		void depthBlt(const Resource& dstResource, const RECT& dstRect,
			const Resource& srcResource, const RECT& srcRect, HANDLE nullResource);
		void displayBlt(const Resource& dstResource, UINT dstSubResourceIndex, const RECT& dstRect,
			const Resource& srcResource, UINT srcSubResourceIndex, const RECT& srcRect);
		void gammaBlt(const Resource& dstResource, UINT dstSubResourceIndex, const RECT& dstRect,
			const Resource& srcResource, UINT srcSubResourceIndex, const RECT& srcRect);
		void lanczosBlt(const Resource& dstResource, UINT dstSubResourceIndex, const RECT& dstRect,
			const Resource& srcResource, UINT srcSubResourceIndex, const RECT& srcRect, UINT lobes);
		void lockRefBlt(const Resource& dstResource, UINT dstSubResourceIndex, const RECT& dstRect,
			const Resource& srcResource, UINT srcSubResourceIndex, const RECT& srcRect,
			const Resource& lockRefResource);
		void palettizedBlt(const Resource& dstResource, UINT dstSubResourceIndex, const RECT& dstRect,
			const Resource& srcResource, UINT srcSubResourceIndex, const RECT& srcRect, RGBQUAD palette[256]);
		void splineBlt(const Resource& dstResource, UINT dstSubResourceIndex, const RECT& dstRect,
			const Resource& srcResource, UINT srcSubResourceIndex, const RECT& srcRect, UINT lobes);
		void textureBlt(const Resource& dstResource, UINT dstSubResourceIndex, const RECT& dstRect,
			const Resource& srcResource, UINT srcSubResourceIndex, const RECT& srcRect,
			UINT filter, const DeviceState::ShaderConstF* srcColorKey = nullptr, const BYTE* alpha = nullptr,
			const Gdi::Region& srcRgn = nullptr);

		static void resetGammaRamp();
		static void setGammaRamp(const D3DDDI_GAMMA_RAMP_RGB256x3x16& ramp);

	private:
		const UINT BLT_SRCALPHA = 1;

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
			const Resource& srcResource, UINT srcSubResourceIndex, const RECT& srcRect, HANDLE pixelShader,
			UINT filter, UINT flags = 0, const BYTE* alpha = nullptr, const Gdi::Region& srcRgn = nullptr);
		void convolution(const Resource& dstResource, UINT dstSubResourceIndex, const RECT& dstRect,
			const Resource& srcResource, UINT srcSubResourceIndex, const RECT& srcRect,
			bool isHorizontal, Float2 support, HANDLE pixelShader,
			const std::function<void(bool)> setExtraParams);
		void convolutionBlt(const Resource& dstResource, UINT dstSubResourceIndex, const RECT& dstRect,
			const Resource& srcResource, UINT srcSubResourceIndex, const RECT& srcRect,
			Float2 support, HANDLE pixelShader, const std::function<void(bool)> setExtraParams = {});

		template <int N>
		std::unique_ptr<void, ResourceDeleter> createPixelShader(const BYTE(&code)[N])
		{
			return createPixelShader(code, N);
		}

		std::unique_ptr<void, ResourceDeleter> createPixelShader(const BYTE* code, UINT size);
		std::unique_ptr<void, ResourceDeleter> createVertexShaderDecl();
		void drawRect(const RECT& srcRect, const RectF& dstRect, UINT srcWidth, UINT srcHeight);
		void setTempTextureStage(UINT stage, const Resource& texture, const RECT& rect, UINT filter);
		void setTextureCoords(UINT stage, const RECT& rect, UINT width, UINT height);

		Device& m_device;
		std::unique_ptr<void, ResourceDeleter> m_psBilinear;
		std::unique_ptr<void, ResourceDeleter> m_psColorKey;
		std::unique_ptr<void, ResourceDeleter> m_psColorKeyBlend;
		std::unique_ptr<void, ResourceDeleter> m_psCubicConvolution[3];
		std::unique_ptr<void, ResourceDeleter> m_psDepthBlt;
		std::unique_ptr<void, ResourceDeleter> m_psDrawCursor;
		std::unique_ptr<void, ResourceDeleter> m_psGamma;
		std::unique_ptr<void, ResourceDeleter> m_psLanczos;
		std::unique_ptr<void, ResourceDeleter> m_psLockRef;
		std::unique_ptr<void, ResourceDeleter> m_psPaletteLookup;
		std::unique_ptr<void, ResourceDeleter> m_psTextureSampler;
		std::unique_ptr<void, ResourceDeleter> m_vertexShaderDecl;
		ConvolutionParams m_convolutionParams;
		std::array<Vertex, 4> m_vertices;
	};
}
