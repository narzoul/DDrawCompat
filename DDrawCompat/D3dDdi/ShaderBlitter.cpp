#include <Common/Log.h>
#include <D3dDdi/Adapter.h>
#include <D3dDdi/Device.h>
#include <D3dDdi/Resource.h>
#include <D3dDdi/ShaderBlitter.h>
#include <D3dDdi/SurfaceRepository.h>
#include <Shaders/DrawCursor.h>
#include <Shaders/GenBilinear.h>
#include <Shaders/PaletteLookup.h>
#include <Shaders/TextureSampler.h>

#define CONCAT_(a, b) a##b
#define CONCAT(a, b) CONCAT_(a, b)
#define SCOPED_STATE(state, ...) DeviceState::Scoped##state CONCAT(scopedState, __LINE__)(m_device.getState(), __VA_ARGS__)

namespace D3dDdi
{
	ShaderBlitter::ShaderBlitter(Device& device)
		: m_device(device)
		, m_psDrawCursor(createPixelShader(g_psDrawCursor))
		, m_psGenBilinear(createPixelShader(g_psGenBilinear))
		, m_psPaletteLookup(createPixelShader(g_psPaletteLookup))
		, m_psTextureSampler(createPixelShader(g_psTextureSampler))
		, m_vertexShaderDecl(createVertexShaderDecl())
	{
	}

	void ShaderBlitter::blt(const Resource& dstResource, UINT dstSubResourceIndex, const RECT& dstRect,
		const Resource& srcResource, const RECT& srcRect, HANDLE pixelShader, UINT filter)
	{
		LOG_FUNC("ShaderBlitter::blt", static_cast<HANDLE>(dstResource), dstSubResourceIndex, dstRect,
			static_cast<HANDLE>(srcResource), srcRect, pixelShader, filter);

		if (!m_vertexShaderDecl || !pixelShader)
		{
			return;
		}

		const auto& dstSurface = dstResource.getFixedDesc().pSurfList[dstSubResourceIndex];
		const auto& srcSurface = srcResource.getFixedDesc().pSurfList[0];

		SCOPED_STATE(RenderState, { D3DDDIRS_SCENECAPTURE, TRUE });
		SCOPED_STATE(VertexShaderDecl, m_vertexShaderDecl);
		SCOPED_STATE(PixelShader, pixelShader);
		SCOPED_STATE(DepthStencil, { nullptr });
		SCOPED_STATE(RenderTarget, { 0, dstResource, dstSubResourceIndex });
		SCOPED_STATE(Viewport, { 0, 0, dstSurface.Width, dstSurface.Height });
		SCOPED_STATE(ZRange, { 0, 1 });

		SCOPED_STATE(RenderState, { D3DDDIRS_ZENABLE, D3DZB_FALSE });
		SCOPED_STATE(RenderState, { D3DDDIRS_FILLMODE, D3DFILL_SOLID });
		SCOPED_STATE(RenderState, { D3DDDIRS_ALPHATESTENABLE, FALSE });
		SCOPED_STATE(RenderState, { D3DDDIRS_CULLMODE, D3DCULL_NONE });
		SCOPED_STATE(RenderState, { D3DDDIRS_DITHERENABLE, FALSE });
		SCOPED_STATE(RenderState, { D3DDDIRS_ALPHABLENDENABLE, FALSE });
		SCOPED_STATE(RenderState, { D3DDDIRS_FOGENABLE, FALSE });
		SCOPED_STATE(RenderState, { D3DDDIRS_STENCILENABLE, FALSE });
		SCOPED_STATE(RenderState, { D3DDDIRS_CLIPPING, FALSE });
		SCOPED_STATE(RenderState, { D3DDDIRS_CLIPPLANEENABLE, 0 });
		SCOPED_STATE(RenderState, { D3DDDIRS_MULTISAMPLEANTIALIAS, FALSE });
		SCOPED_STATE(RenderState, { D3DDDIRS_COLORWRITEENABLE, 0xF });
		SCOPED_STATE(RenderState, { D3DDDIRS_SRGBWRITEENABLE, D3DTEXF_LINEAR == filter });

		SCOPED_STATE(Texture, 0, srcResource, filter);

		struct Vertex
		{
			float x;
			float y;
			float z;
			float rhw;
			float tu;
			float tv;
		};

		const float srcWidth = static_cast<float>(srcSurface.Width);
		const float srcHeight = static_cast<float>(srcSurface.Height);

		Vertex vertices[4] = {
			{ dstRect.left - 0.5f, dstRect.top - 0.5f, 0, 1, srcRect.left / srcWidth, srcRect.top / srcHeight },
			{ dstRect.right - 0.5f, dstRect.top - 0.5f, 0, 1, srcRect.right / srcWidth, srcRect.top / srcHeight },
			{ dstRect.right - 0.5f, dstRect.bottom - 0.5f, 0, 1, srcRect.right / srcWidth, srcRect.bottom / srcHeight },
			{ dstRect.left - 0.5f, dstRect.bottom - 0.5f, 0, 1, srcRect.left / srcWidth, srcRect.bottom / srcHeight }
		};

		D3DDDIARG_SETSTREAMSOURCEUM um = {};
		um.Stride = sizeof(Vertex);
		SCOPED_STATE(StreamSourceUm, um, vertices);

		D3DDDIARG_DRAWPRIMITIVE dp = {};
		dp.PrimitiveType = D3DPT_TRIANGLEFAN;
		dp.VStart = 0;
		dp.PrimitiveCount = 2;
		m_device.pfnDrawPrimitive(&dp, nullptr);
		m_device.flushPrimitives();
	}

	HANDLE ShaderBlitter::createPixelShader(const BYTE* code, UINT size)
	{
		D3DDDIARG_CREATEPIXELSHADER data = {};
		data.CodeSize = size;
		if (FAILED(m_device.getOrigVtable().pfnCreatePixelShader(m_device, &data, reinterpret_cast<const UINT*>(code))))
		{
			return nullptr;
		}
		return data.ShaderHandle;
	}

	HANDLE ShaderBlitter::createVertexShaderDecl()
	{
		const UINT D3DDECLTYPE_FLOAT2 = 1;
		const UINT D3DDECLTYPE_FLOAT4 = 3;
		const UINT D3DDECLMETHOD_DEFAULT = 0;
		const UINT D3DDECLUSAGE_TEXCOORD = 5;
		const UINT D3DDECLUSAGE_POSITIONT = 9;

		const D3DDDIVERTEXELEMENT vertexElements[] = {
			{ 0, 0, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITIONT, 0 },
			{ 0, 16, D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0 }
		};

		D3DDDIARG_CREATEVERTEXSHADERDECL data = {};
		data.NumVertexElements = sizeof(vertexElements) / sizeof(vertexElements[0]);

		if (FAILED(m_device.getOrigVtable().pfnCreateVertexShaderDecl(m_device, &data, vertexElements)))
		{
			return nullptr;
		}
		return data.ShaderHandle;
	}

	void ShaderBlitter::cursorBlt(const Resource& dstResource, UINT dstSubResourceIndex, HCURSOR cursor, POINT pt)
	{
		LOG_FUNC("ShaderBlitter::cursorBlt", static_cast<HANDLE>(dstResource), dstSubResourceIndex, cursor, pt);

		auto& repo = SurfaceRepository::get(m_device.getAdapter());
		auto cur = repo.getCursor(cursor);
		auto xorTexture = repo.getLogicalXorTexture();

		pt.x -= cur.hotspot.x;
		pt.y -= cur.hotspot.y;
		RECT dstRect = { pt.x, pt.y, pt.x + cur.size.cx, pt.y + cur.size.cy };

		auto& dstDesc = dstResource.getFixedDesc().pSurfList[dstSubResourceIndex];
		RECT clippedDstRect = {};
		clippedDstRect.right = dstDesc.Width;
		clippedDstRect.bottom = dstDesc.Height;
		IntersectRect(&clippedDstRect, &clippedDstRect, &dstRect);

		if (!cur.maskTexture || !cur.colorTexture || !cur.tempTexture || !xorTexture || IsRectEmpty(&clippedDstRect))
		{
			return;
		}

		RECT clippedSrcRect = clippedDstRect;
		OffsetRect(&clippedSrcRect, -dstRect.left, -dstRect.top);

		D3DDDIARG_BLT data = {};
		data.hSrcResource = dstResource;
		data.SrcSubResourceIndex = dstSubResourceIndex;
		data.SrcRect = clippedDstRect;
		data.hDstResource = *cur.tempTexture;
		data.DstRect = clippedSrcRect;
		m_device.getOrigVtable().pfnBlt(m_device, &data);

		SCOPED_STATE(Texture, 1, *cur.maskTexture, D3DTEXF_POINT);
		SCOPED_STATE(Texture, 2, *cur.colorTexture, D3DTEXF_POINT);
		SCOPED_STATE(Texture, 3, *xorTexture, D3DTEXF_POINT);
		blt(dstResource, dstSubResourceIndex, clippedDstRect, *cur.tempTexture, clippedSrcRect, m_psDrawCursor, D3DTEXF_POINT);
	}

	void ShaderBlitter::genBilinearBlt(const Resource& dstResource, UINT dstSubResourceIndex, const RECT& dstRect,
		const Resource& srcResource, const RECT& srcRect, UINT blurPercent)
	{
		if (100 == blurPercent)
		{
			blt(dstResource, dstSubResourceIndex, dstRect, srcResource, srcRect, m_psTextureSampler, D3DTEXF_LINEAR);
			return;
		}

		const auto& srcDesc = srcResource.getFixedDesc().pSurfList[0];
		float scaleX = static_cast<float>(dstRect.right - dstRect.left) / (srcRect.right - srcRect.left);
		float scaleY = static_cast<float>(dstRect.bottom - dstRect.top) / (srcRect.bottom - srcRect.top);

		const float blur = blurPercent / 100.0f;
		scaleX = 1 / ((1 - blur) / scaleX + blur);
		scaleY = 1 / ((1 - blur) / scaleY + blur);

		const DeviceState::ShaderConstF registers[] = {
			{ static_cast<float>(srcDesc.Width), static_cast<float>(srcDesc.Height), 0.0f, 0.0f },
			{ scaleX, scaleY, 0.0f, 0.0f }
		};

		SCOPED_STATE(PixelShaderConst, { 0, sizeof(registers) / sizeof(registers[0]) }, registers);

		blt(dstResource, dstSubResourceIndex, dstRect, srcResource, srcRect, m_psGenBilinear, D3DTEXF_LINEAR);
	}

	void ShaderBlitter::palettizedBlt(const Resource& dstResource, UINT dstSubResourceIndex,
		const Resource& srcResource, RGBQUAD palette[256])
	{
		LOG_FUNC("ShaderBlitter::palettizedBlt", static_cast<HANDLE>(dstResource), dstSubResourceIndex,
			static_cast<HANDLE>(srcResource), Compat::array(reinterpret_cast<void**>(palette), 256));

		auto paletteTexture(SurfaceRepository::get(m_device.getAdapter()).getPaletteTexture());
		if (!paletteTexture)
		{
			return;
		}

		D3DDDIARG_LOCK lock = {};
		lock.hResource = *paletteTexture;
		lock.Flags.Discard = 1;
		m_device.getOrigVtable().pfnLock(m_device, &lock);
		if (!lock.pSurfData)
		{
			return;
		}

		memcpy(lock.pSurfData, palette, 256 * sizeof(RGBQUAD));

		D3DDDIARG_UNLOCK unlock = {};
		unlock.hResource = *paletteTexture;
		m_device.getOrigVtable().pfnUnlock(m_device, &unlock);

		const auto& dstSurface = dstResource.getFixedDesc().pSurfList[dstSubResourceIndex];
		const auto& srcSurface = srcResource.getFixedDesc().pSurfList[0];
		const RECT dstRect = { 0, 0, static_cast<LONG>(dstSurface.Width), static_cast<LONG>(dstSurface.Height) };
		const RECT srcRect = { 0, 0, static_cast<LONG>(srcSurface.Width), static_cast<LONG>(srcSurface.Height) };

		SCOPED_STATE(Texture, 1, *paletteTexture, D3DTEXF_POINT);
		blt(dstResource, dstSubResourceIndex, dstRect, srcResource, srcRect, m_psPaletteLookup, D3DTEXF_POINT);
	}
}
