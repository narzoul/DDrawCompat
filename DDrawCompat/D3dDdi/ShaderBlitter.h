#pragma once

#include <memory>

#include <Windows.h>

#include <D3dDdi/ResourceDeleter.h>

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

		void cursorBlt(const Resource& dstResource, UINT dstSubResourceIndex, HCURSOR cursor, POINT pt);
		void genBilinearBlt(const Resource& dstResource, UINT dstSubResourceIndex, const RECT& dstRect,
			const Resource& srcResource, const RECT& srcRect, UINT blurPercent);
		void palettizedBlt(const Resource& dstResource, UINT dstSubResourceIndex,
			const Resource& srcResource, RGBQUAD palette[256]);

	private:
		void blt(const Resource& dstResource, UINT dstSubResourceIndex, const RECT& dstRect,
			const Resource& srcResource, const RECT& srcRect, HANDLE pixelShader, UINT filter);

		template <int N>
		std::unique_ptr<void, ResourceDeleter> createPixelShader(const BYTE(&code)[N])
		{
			return createPixelShader(code, N);
		}

		std::unique_ptr<void, ResourceDeleter> createPixelShader(const BYTE* code, UINT size);
		std::unique_ptr<void, ResourceDeleter> createVertexShaderDecl();
		void setTempTextureStage(UINT stage, HANDLE texture, UINT filter);

		Device& m_device;
		std::unique_ptr<void, ResourceDeleter> m_psDrawCursor;
		std::unique_ptr<void, ResourceDeleter> m_psGenBilinear;
		std::unique_ptr<void, ResourceDeleter> m_psPaletteLookup;
		std::unique_ptr<void, ResourceDeleter> m_psTextureSampler;
		std::unique_ptr<void, ResourceDeleter> m_vertexShaderDecl;
	};
}
