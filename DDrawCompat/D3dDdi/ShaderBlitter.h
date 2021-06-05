#pragma once

#include <ddraw.h>

#include <Common/CompatWeakPtr.h>

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

		void palettizedBlt(const Resource& dstResource, UINT dstSubResourceIndex,
			const Resource& srcResource, RGBQUAD palette[256]);

	private:
		void blt(const Resource& dstResource, UINT dstSubResourceIndex, const RECT& dstRect,
			const Resource& srcResource, const RECT& srcRect, HANDLE pixelShader, UINT filter);

		HANDLE createPixelShader(const BYTE* code, UINT size);
		HANDLE createVertexShaderDecl();

		Device& m_device;
		CompatWeakPtr<IDirectDrawSurface7> m_paletteTexture;
		HANDLE m_psPaletteLookup;
		HANDLE m_vertexShaderDecl;
	};
}
