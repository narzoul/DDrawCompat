#pragma once

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
		void genBilinearBlt(const Resource& dstResource, UINT dstSubResourceIndex, const RECT& dstRect,
			const Resource& srcResource, const RECT& srcRect, UINT blurPercent);
		void palettizedBlt(const Resource& dstResource, UINT dstSubResourceIndex, const RECT& dstRect,
			const Resource& srcResource, const RECT& srcRect, RGBQUAD palette[256]);
		void textureBlt(const Resource& dstResource, UINT dstSubResourceIndex, const RECT& dstRect,
			const Resource& srcResource, UINT srcSubResourceIndex, const RECT& srcRect,
			UINT filter, const UINT* srcColorKey = nullptr, const BYTE* alpha = nullptr,
			const Gdi::Region& srcRgn = nullptr);

	private:
		struct Vertex
		{
			float x;
			float y;
			float z;
			float rhw;
			float tu;
			float tv;
		};

		void blt(const Resource& dstResource, UINT dstSubResourceIndex, const RECT& dstRect,
			const Resource& srcResource, UINT srcSubResourceIndex, const RECT& srcRect,
			HANDLE pixelShader, UINT filter, const UINT* srcColorKey = nullptr, const BYTE* alpha = nullptr,
			const Gdi::Region& srcRgn = nullptr);

		template <int N>
		std::unique_ptr<void, ResourceDeleter> createPixelShader(const BYTE(&code)[N])
		{
			return createPixelShader(code, N);
		}

		std::unique_ptr<void, ResourceDeleter> createPixelShader(const BYTE* code, UINT size);
		std::unique_ptr<void, ResourceDeleter> createVertexShaderDecl();
		void drawRect(Vertex(&vertices)[4], const RECT& srcRect, const RectF& dstRect, float srcWidth, float srcHeight);
		void setTempTextureStage(UINT stage, HANDLE texture, UINT filter, const UINT* srcColorKey = nullptr);

		Device& m_device;
		std::unique_ptr<void, ResourceDeleter> m_psDrawCursor;
		std::unique_ptr<void, ResourceDeleter> m_psGenBilinear;
		std::unique_ptr<void, ResourceDeleter> m_psPaletteLookup;
		std::unique_ptr<void, ResourceDeleter> m_psTextureSampler;
		std::unique_ptr<void, ResourceDeleter> m_vertexShaderDecl;
	};
}
