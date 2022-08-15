#pragma once

#include <array>
#include <memory>

#include <Windows.h>

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

		void cursorBlt(const Resource& dstResource, UINT dstSubResourceIndex, const RECT& dstRect,
			HCURSOR cursor, POINT pt);
		void gammaBlt(const Resource& dstResource, UINT dstSubResourceIndex, const RECT& dstRect,
			const Resource& srcResource, const RECT& srcRect);
		void genBilinearBlt(const Resource& dstResource, UINT dstSubResourceIndex, const RECT& dstRect,
			const Resource& srcResource, const RECT& srcRect, UINT blurPercent);
		void lockRefBlt(const Resource& dstResource, UINT dstSubResourceIndex, const RECT& dstRect,
			const Resource& srcResource, UINT srcSubResourceIndex, const RECT& srcRect,
			const Resource& lockRefResource);
		void palettizedBlt(const Resource& dstResource, UINT dstSubResourceIndex, const RECT& dstRect,
			const Resource& srcResource, UINT srcSubResourceIndex, const RECT& srcRect, RGBQUAD palette[256]);
		void textureBlt(const Resource& dstResource, UINT dstSubResourceIndex, const RECT& dstRect,
			const Resource& srcResource, UINT srcSubResourceIndex, const RECT& srcRect,
			UINT filter, const DeviceState::ShaderConstF* srcColorKey = nullptr, const BYTE* alpha = nullptr,
			const Gdi::Region& srcRgn = nullptr);

		static bool isGammaRampDefault();
		static void resetGammaRamp();
		static void setGammaRamp(const D3DDDI_GAMMA_RAMP_RGB256x3x16& ramp);

	private:
		struct Vertex
		{
			std::array<float, 2> xy;
			float z;
			float rhw;
			std::array<float, 2> tc[4];
		};

		void blt(const Resource& dstResource, UINT dstSubResourceIndex, const RECT& dstRect,
			const Resource& srcResource, UINT srcSubResourceIndex, const RECT& srcRect,
			HANDLE pixelShader, UINT filter, const BYTE* alpha = nullptr, const Gdi::Region& srcRgn = nullptr);

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
		std::unique_ptr<void, ResourceDeleter> m_psColorKey;
		std::unique_ptr<void, ResourceDeleter> m_psDrawCursor;
		std::unique_ptr<void, ResourceDeleter> m_psGamma;
		std::unique_ptr<void, ResourceDeleter> m_psGenBilinear;
		std::unique_ptr<void, ResourceDeleter> m_psLockRef;
		std::unique_ptr<void, ResourceDeleter> m_psPaletteLookup;
		std::unique_ptr<void, ResourceDeleter> m_psTextureSampler;
		std::unique_ptr<void, ResourceDeleter> m_vertexShaderDecl;
		std::array<Vertex, 4> m_vertices;
	};
}
