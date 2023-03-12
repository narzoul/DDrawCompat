#include <Common/Log.h>
#include <Common/Rect.h>
#include <Config/Settings/DisplayFilter.h>
#include <D3dDdi/Adapter.h>
#include <D3dDdi/Device.h>
#include <D3dDdi/Resource.h>
#include <D3dDdi/ShaderBlitter.h>
#include <D3dDdi/SurfaceRepository.h>
#include <DDraw/Surfaces/PrimarySurface.h>
#include <Shaders/Bicubic.h>
#include <Shaders/ColorKey.h>
#include <Shaders/ColorKeyBlend.h>
#include <Shaders/DepthBlt.h>
#include <Shaders/DrawCursor.h>
#include <Shaders/Gamma.h>
#include <Shaders/GenBilinear.h>
#include <Shaders/Lanczos.h>
#include <Shaders/LockRef.h>
#include <Shaders/PaletteLookup.h>
#include <Shaders/TextureSampler.h>

#define CONCAT_(a, b) a##b
#define CONCAT(a, b) CONCAT_(a, b)
#define SCOPED_STATE(state, ...) DeviceState::Scoped##state CONCAT(scopedState, __LINE__)(m_device.getState(), __VA_ARGS__)

namespace
{
	D3DDDI_GAMMA_RAMP_RGB256x3x16 g_gammaRamp;
	bool g_isGammaRampDefault = true;
	bool g_isGammaRampInvalidated = false;

	D3dDdi::DeviceState::ShaderConstF getColorKeyAsFloat4(const UINT* colorKey)
	{
		std::array<float, 4> ck{};
		if (colorKey)
		{
			ck[0] = ((*colorKey & 0xFF0000) >> 16) / 255.0f;
			ck[1] = ((*colorKey & 0x00FF00) >> 8) / 255.0f;
			ck[2] = ((*colorKey & 0x0000FF)) / 255.0f;
		}
		else
		{
			ck[0] = ck[1] = ck[2] = -1.0f;
		}
		return ck;
	}

	void setGammaValues(BYTE* ptr, USHORT* ramp)
	{
		for (UINT i = 0; i < 256; ++i)
		{
			ptr[i] = static_cast<BYTE>(ramp[i] * 0xFF / 0xFFFF);
		}
	}
}

namespace D3dDdi
{
	ShaderBlitter::ShaderBlitter(Device& device)
		: m_device(device)
		, m_psBicubic(createPixelShader(g_psBicubic))
		, m_psColorKey(createPixelShader(g_psColorKey))
		, m_psColorKeyBlend(createPixelShader(g_psColorKeyBlend))
		, m_psDepthBlt(createPixelShader(g_psDepthBlt))
		, m_psDrawCursor(createPixelShader(g_psDrawCursor))
		, m_psGamma(createPixelShader(g_psGamma))
		, m_psGenBilinear(createPixelShader(g_psGenBilinear))
		, m_psLanczos(createPixelShader(g_psLanczos))
		, m_psLockRef(createPixelShader(g_psLockRef))
		, m_psPaletteLookup(createPixelShader(g_psPaletteLookup))
		, m_psTextureSampler(createPixelShader(g_psTextureSampler))
		, m_vertexShaderDecl(createVertexShaderDecl())
		, m_convolutionBaseParams{}
		, m_convolutionExtraParams{}
		, m_vertices{}
	{
		for (std::size_t i = 0; i < m_vertices.size(); ++i)
		{
			m_vertices[i].rhw = 1;
		}
	}

	void ShaderBlitter::bicubicBlt(const Resource& dstResource, UINT dstSubResourceIndex, const RECT& dstRect,
		const Resource& srcResource, UINT srcSubResourceIndex, const RECT& srcRect, UINT blurPercent)
	{
		LOG_FUNC("ShaderBlitter::bicubicBlt", static_cast<HANDLE>(dstResource), dstSubResourceIndex, dstRect,
			static_cast<HANDLE>(srcResource), srcSubResourceIndex, srcRect, blurPercent);

		const float B = blurPercent / 100.0f;
		const float C = (1 - B) / 2;

		m_convolutionExtraParams[0] = { (12 - 9 * B - 6 * C) / 6, (-18 + 12 * B + 6 * C) / 6, 0, (6 - 2 * B) / 6 };
		m_convolutionExtraParams[1] = { (-B - 6 * C) / 6, (6 * B + 30 * C) / 6, (-12 * B - 48 * C) / 6, (8 * B + 24 * C) / 6 };

		convolutionBlt(dstResource, dstSubResourceIndex, dstRect, srcResource, srcSubResourceIndex, srcRect,
			2, m_psBicubic.get());
	}

	void ShaderBlitter::blt(const Resource& dstResource, UINT dstSubResourceIndex, const RECT& dstRect,
		const Resource& srcResource, UINT srcSubResourceIndex, const RECT& srcRect,
		HANDLE pixelShader, UINT filter, UINT flags, const BYTE* alpha, const Gdi::Region& srcRgn)
	{
		LOG_FUNC("ShaderBlitter::blt", static_cast<HANDLE>(dstResource), dstSubResourceIndex, dstRect,
			static_cast<HANDLE>(srcResource), srcSubResourceIndex, srcRect, pixelShader, filter,
			Compat::hex(flags), alpha, static_cast<HRGN>(srcRgn));

		if (!m_vertexShaderDecl || !pixelShader)
		{
			return;
		}

		const auto& srcSurface = srcResource.getFixedDesc().pSurfList[srcSubResourceIndex];
		const auto& dstSurface = dstResource.getFixedDesc().pSurfList[dstSubResourceIndex];

		const bool srgb = (filter & D3DTEXF_SRGB) &&
			(srcResource.getFormatOp().Operations & FORMATOP_SRGBREAD) &&
			(dstResource.getFormatOp().Operations & FORMATOP_SRGBWRITE);

		auto& state = m_device.getState();
		state.setSpriteMode(false);
		state.setTempRenderTarget({ 0, dstResource, dstSubResourceIndex });
		state.setTempDepthStencil({ nullptr });
		state.setTempViewport({ 0, 0, dstSurface.Width, dstSurface.Height });
		state.setTempZRange({ 0, 1 });
		state.setTempPixelShader(pixelShader);
		state.setTempVertexShaderDecl(m_vertexShaderDecl.get());

		state.setTempRenderState({ D3DDDIRS_SCENECAPTURE, TRUE });
		state.setTempRenderState({ D3DDDIRS_ZENABLE, D3DZB_FALSE });
		state.setTempRenderState({ D3DDDIRS_FILLMODE, D3DFILL_SOLID });
		state.setTempRenderState({ D3DDDIRS_ZWRITEENABLE, FALSE });
		state.setTempRenderState({ D3DDDIRS_ALPHATESTENABLE, FALSE });
		state.setTempRenderState({ D3DDDIRS_CULLMODE, D3DCULL_NONE });
		state.setTempRenderState({ D3DDDIRS_DITHERENABLE, FALSE });
		state.setTempRenderState({ D3DDDIRS_ALPHABLENDENABLE, (flags & BLT_SRCALPHA) || alpha});
		state.setTempRenderState({ D3DDDIRS_FOGENABLE, FALSE });
		state.setTempRenderState({ D3DDDIRS_COLORKEYENABLE, FALSE });
		state.setTempRenderState({ D3DDDIRS_STENCILENABLE, FALSE });
		state.setTempRenderState({ D3DDDIRS_CLIPPING, FALSE });
		state.setTempRenderState({ D3DDDIRS_CLIPPLANEENABLE, 0 });
		state.setTempRenderState({ D3DDDIRS_MULTISAMPLEANTIALIAS, FALSE });
		state.setTempRenderState({ D3DDDIRS_COLORWRITEENABLE, 0xF });
		state.setTempRenderState({ D3DDDIRS_SCISSORTESTENABLE, FALSE });
		state.setTempRenderState({ D3DDDIRS_SRGBWRITEENABLE, srgb });

		if (alpha)
		{
			const UINT D3DBLEND_BLENDFACTOR = 14;
			const UINT D3DBLEND_INVBLENDFACTOR = 15;
			state.setTempRenderState({ D3DDDIRS_SRCBLEND, D3DBLEND_BLENDFACTOR });
			state.setTempRenderState({ D3DDDIRS_DESTBLEND, D3DBLEND_INVBLENDFACTOR });

			const D3DCOLOR blendFactor = (*alpha << 16) | (*alpha << 8) | *alpha;
			state.setTempRenderState({ D3DDDIRS_BLENDFACTOR, blendFactor });
		}
		else if (flags & BLT_SRCALPHA)
		{
			const UINT D3DBLEND_SRCALPHA = 5;
			const UINT D3DBLEND_INVSRCALPHA = 6;
			state.setTempRenderState({ D3DDDIRS_SRCBLEND, D3DBLEND_SRCALPHA });
			state.setTempRenderState({ D3DDDIRS_DESTBLEND, D3DBLEND_INVSRCALPHA });
		}

		setTempTextureStage(0, srcResource, srcRect, LOWORD(filter));
		state.setTempTextureStageState({ 0, D3DDDITSS_SRGBTEXTURE, srgb });

		state.setTempStreamSourceUm({ 0, sizeof(Vertex) }, m_vertices.data());

		DeviceState::TempStateLock lock(state);

		if (srcRgn)
		{
			auto srcRects(srcRgn.getRects());
			for (const auto& sr : srcRects)
			{
				RectF dr = Rect::toRectF(sr);
				Rect::transform(dr, srcRect, dstRect);
				drawRect(sr, dr, srcSurface.Width, srcSurface.Height);
			}
		}
		else
		{
			drawRect(srcRect, Rect::toRectF(dstRect), srcSurface.Width, srcSurface.Height);
		}

		m_device.flushPrimitives();
	}

	void ShaderBlitter::colorKeyBlt(const Resource& dstResource, UINT dstSubResourceIndex,
		const Resource& srcResource, UINT srcSubResourceIndex, DeviceState::ShaderConstF srcColorKey)
	{
		DeviceState::TempPixelShaderConst psConst(m_device.getState(), { 31, 1 }, &srcColorKey);
		blt(dstResource, dstSubResourceIndex, dstResource.getRect(dstSubResourceIndex),
			srcResource, srcSubResourceIndex, srcResource.getRect(srcSubResourceIndex),
			m_psColorKeyBlend.get(), D3DTEXF_POINT);
	}

	void ShaderBlitter::convolution(const Resource& dstResource, UINT dstSubResourceIndex, const RECT& dstRect,
		const Resource& srcResource, UINT srcSubResourceIndex, const RECT& srcRect,
		bool isHorizontal, float kernelStep, int sampleCount, float support, HANDLE pixelShader)
	{
		LOG_FUNC("ShaderBlitter::convolution", static_cast<HANDLE>(dstResource), dstSubResourceIndex, dstRect,
			static_cast<HANDLE>(srcResource), srcSubResourceIndex, srcRect,
			isHorizontal, kernelStep, sampleCount, support, pixelShader);

		const auto& srcDesc = srcResource.getFixedDesc().pSurfList[0];
		const Float2 textureSize(srcDesc.Width, srcDesc.Height);
		const Float2 textureSizeRcp = Float2(1) / textureSize;
		const float textureStep = isHorizontal ? textureSizeRcp.x : textureSizeRcp.y;

		const int firstSampleOffset = -sampleCount / 2 + 1;
		const float firstKernelOffset = firstSampleOffset * kernelStep;
		const float firstTextureOffset = firstSampleOffset * textureStep;

		m_convolutionBaseParams[0] = { textureSize.x, textureSize.y, textureSizeRcp.x, textureSizeRcp.y };
		if (isHorizontal)
		{
			m_convolutionBaseParams[1] = { firstTextureOffset, 0, firstKernelOffset, 0 };
			m_convolutionBaseParams[2] = { textureStep, 0, kernelStep, 0 };
			m_convolutionBaseParams[3] = { -0.5f, 0, 0.5f * textureSizeRcp.x, 0.5f * textureSizeRcp.y };
		}
		else
		{
			m_convolutionBaseParams[1] = { 0, firstTextureOffset, 0, firstKernelOffset };
			m_convolutionBaseParams[2] = { 0, textureStep, 0, kernelStep };
			m_convolutionBaseParams[3] = { 0, -0.5f, 0.5f * textureSizeRcp.x, 0.5f * textureSizeRcp.y };
		}
		m_convolutionBaseParams[4] = { support, 1.0f / support, 0, 0 };

		DeviceState::TempPixelShaderConst tempPsConst(m_device.getState(),
			{ 0, m_convolutionBaseParams.size() + m_convolutionExtraParams.size()}, m_convolutionBaseParams.data());
		blt(dstResource, dstSubResourceIndex, dstRect, srcResource, srcSubResourceIndex, srcRect,
			pixelShader, D3DTEXF_LINEAR | D3DTEXF_SRGB);
	}

	void ShaderBlitter::convolutionBlt(const Resource& dstResource, UINT dstSubResourceIndex, const RECT& dstRect,
		const Resource& srcResource, UINT srcSubResourceIndex, const RECT& srcRect,
		Float2 support, HANDLE pixelShader)
	{
		LOG_FUNC("ShaderBlitter::convolutionBlt", static_cast<HANDLE>(dstResource), dstSubResourceIndex, dstRect,
			static_cast<HANDLE>(srcResource), srcSubResourceIndex, srcRect, support, pixelShader);

		const Int2 dstSize(dstRect.right - dstRect.left, dstRect.bottom - dstRect.top);
		const Int2 srcSize(srcRect.right - srcRect.left, srcRect.bottom - srcRect.top);
		const Float2 scale = Float2(dstSize) / Float2(srcSize);
		const Float2 kernelStep = min(scale, Float2(1));
		const Int2 sampleCount = min(Float2(2) * ceil(support / kernelStep), Float2(255));

		if (srcSize.y == dstSize.y)
		{
			return convolution(dstResource, dstSubResourceIndex, dstRect, srcResource, srcSubResourceIndex, srcRect,
				true, kernelStep.x, sampleCount.x, support.x, pixelShader);
		}
		else if (srcSize.x == dstSize.x)
		{
			return convolution(dstResource, dstSubResourceIndex, dstRect, srcResource, srcSubResourceIndex, srcRect,
				false, kernelStep.y, sampleCount.y, support.y, pixelShader);
		}

		const bool isHorizontalFirst = dstSize.x * srcSize.y <= srcSize.x * dstSize.y;
		RECT rect = { 0, 0, srcSize.x, srcSize.y };
		if (dstSize.x * srcSize.y <= srcSize.x * dstSize.y)
		{
			rect.right = dstSize.x;
		}
		else
		{
			rect.bottom = dstSize.y;
		}

		auto rt = m_device.getRepo().getNextRenderTarget(rect.right, rect.bottom, &srcResource, &dstResource).resource;
		if (!rt)
		{
			return;
		}

		const std::array<DeviceState::ShaderConstI, 2> reg = { {
			{ sampleCount.x },
			{ sampleCount.y }
		} };

		DeviceState::TempPixelShaderConstI tempPsConstI(m_device.getState(), { 0, reg.size()}, reg.data());

		if (isHorizontalFirst)
		{
			convolution(*rt, 0, rect, srcResource, srcSubResourceIndex, srcRect,
				true, kernelStep.x, sampleCount.x, support.x, pixelShader);
			convolution(dstResource, dstSubResourceIndex, dstRect, *rt, 0, rect,
				false, kernelStep.y, sampleCount.y, support.y, pixelShader);
		}
		else
		{
			convolution(*rt, 0, rect, srcResource, srcSubResourceIndex, srcRect,
				false, kernelStep.y, sampleCount.y, support.y, pixelShader);
			convolution(dstResource, dstSubResourceIndex, dstRect, *rt, 0, rect,
				true, kernelStep.x, sampleCount.x, support.x, pixelShader);
		}
	}

	std::unique_ptr<void, ResourceDeleter> ShaderBlitter::createPixelShader(const BYTE* code, UINT size)
	{
		D3DDDIARG_CREATEPIXELSHADER data = {};
		data.CodeSize = size;
		if (FAILED(m_device.getOrigVtable().pfnCreatePixelShader(m_device, &data, reinterpret_cast<const UINT*>(code))))
		{
			return nullptr;
		}
		return { data.ShaderHandle, ResourceDeleter(m_device, m_device.getOrigVtable().pfnDeletePixelShader) };
	}

	std::unique_ptr<void, ResourceDeleter> ShaderBlitter::createVertexShaderDecl()
	{
		const UINT D3DDECLTYPE_FLOAT2 = 1;
		const UINT D3DDECLTYPE_FLOAT4 = 3;
		const UINT D3DDECLMETHOD_DEFAULT = 0;
		const UINT D3DDECLUSAGE_TEXCOORD = 5;
		const UINT D3DDECLUSAGE_POSITIONT = 9;

		const D3DDDIVERTEXELEMENT vertexElements[] = {
			{ 0, 0, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITIONT, 0 },
			{ 0, 16, D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0 },
			{ 0, 24, D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 1 },
			{ 0, 32, D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 2 },
			{ 0, 40, D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 3 }
		};

		D3DDDIARG_CREATEVERTEXSHADERDECL data = {};
		data.NumVertexElements = sizeof(vertexElements) / sizeof(vertexElements[0]);

		if (FAILED(m_device.getOrigVtable().pfnCreateVertexShaderDecl(m_device, &data, vertexElements)))
		{
			return nullptr;
		}
		return { data.ShaderHandle, ResourceDeleter(m_device, m_device.getOrigVtable().pfnDeleteVertexShaderDecl) };
	}

	void ShaderBlitter::cursorBlt(const Resource& dstResource, UINT dstSubResourceIndex, const RECT& dstRect,
		HCURSOR cursor, POINT pt)
	{
		LOG_FUNC("ShaderBlitter::cursorBlt", static_cast<HANDLE>(dstResource), dstSubResourceIndex, dstRect, cursor, pt);

		auto& repo = m_device.getRepo();
		auto cur = repo.getCursor(cursor);
		if (!cur.colorTexture)
		{
			return;
		}

		Resource* xorTexture = nullptr;
		if (cur.maskTexture)
		{
			xorTexture = repo.getLogicalXorTexture();
			if (!xorTexture)
			{
				return;
			}
		}

		pt.x -= cur.hotspot.x;
		pt.y -= cur.hotspot.y;
		RECT srcRect = { pt.x, pt.y, pt.x + cur.size.cx, pt.y + cur.size.cy };

		RECT monitorRect = DDraw::PrimarySurface::getMonitorRect();
		RECT clippedSrcRect = {};
		IntersectRect(&clippedSrcRect, &srcRect, &monitorRect);
		if (IsRectEmpty(&clippedSrcRect))
		{
			return;
		}

		RECT clippedDstRect = clippedSrcRect;
		Rect::transform(clippedDstRect, monitorRect, dstRect);

		OffsetRect(&clippedSrcRect, -srcRect.left, -srcRect.top);

		if (cur.maskTexture)
		{
			D3DDDIARG_BLT data = {};
			data.hSrcResource = dstResource;
			data.SrcSubResourceIndex = dstSubResourceIndex;
			data.SrcRect = clippedDstRect;
			data.hDstResource = *cur.tempTexture;
			data.DstRect = clippedSrcRect;
			m_device.getOrigVtable().pfnBlt(m_device, &data);

			setTempTextureStage(1, *cur.maskTexture, clippedSrcRect, D3DTEXF_POINT);
			setTempTextureStage(2, *cur.colorTexture, clippedSrcRect, D3DTEXF_POINT);
			setTempTextureStage(3, *xorTexture, clippedSrcRect, D3DTEXF_POINT);
			blt(dstResource, dstSubResourceIndex, clippedDstRect, *cur.tempTexture, 0, clippedSrcRect,
				m_psDrawCursor.get(), D3DTEXF_POINT);
		}
		else
		{
			blt(dstResource, dstSubResourceIndex, clippedDstRect, *cur.colorTexture, 0, clippedSrcRect,
				m_psTextureSampler.get(), D3DTEXF_POINT, BLT_SRCALPHA);
		}
	}

	void ShaderBlitter::depthBlt(const Resource& dstResource, const RECT& dstRect,
		const Resource& srcResource, const RECT& srcRect, HANDLE nullResource)
	{
		LOG_FUNC("ShaderBlitter::depthBlt", static_cast<HANDLE>(dstResource), dstRect,
			static_cast<HANDLE>(srcResource), srcRect, nullResource);

		const auto& srcSurface = srcResource.getFixedDesc().pSurfList[0];
		const auto& dstSurface = dstResource.getFixedDesc().pSurfList[0];

		auto& state = m_device.getState();
		state.setSpriteMode(false);
		state.setTempRenderTarget({ 0, nullResource, 0 });
		state.setTempDepthStencil({ dstResource });
		state.setTempViewport({ 0, 0, dstSurface.Width, dstSurface.Height });
		state.setTempZRange({ 0, 1 });
		state.setTempPixelShader(m_psDepthBlt.get());
		state.setTempVertexShaderDecl(m_vertexShaderDecl.get());

		state.setTempRenderState({ D3DDDIRS_SCENECAPTURE, TRUE });
		state.setTempRenderState({ D3DDDIRS_ZENABLE, D3DZB_TRUE });
		state.setTempRenderState({ D3DDDIRS_ZFUNC, D3DCMP_ALWAYS });
		state.setTempRenderState({ D3DDDIRS_FILLMODE, D3DFILL_SOLID });
		state.setTempRenderState({ D3DDDIRS_ZWRITEENABLE, TRUE });
		state.setTempRenderState({ D3DDDIRS_ALPHATESTENABLE, FALSE });
		state.setTempRenderState({ D3DDDIRS_CULLMODE, D3DCULL_NONE });
		state.setTempRenderState({ D3DDDIRS_DITHERENABLE, FALSE });
		state.setTempRenderState({ D3DDDIRS_ALPHABLENDENABLE, FALSE });
		state.setTempRenderState({ D3DDDIRS_FOGENABLE, FALSE });
		state.setTempRenderState({ D3DDDIRS_COLORKEYENABLE, FALSE });
		state.setTempRenderState({ D3DDDIRS_STENCILENABLE, FALSE });
		state.setTempRenderState({ D3DDDIRS_CLIPPING, FALSE });
		state.setTempRenderState({ D3DDDIRS_CLIPPLANEENABLE, 0 });
		state.setTempRenderState({ D3DDDIRS_MULTISAMPLEANTIALIAS, FALSE });
		state.setTempRenderState({ D3DDDIRS_COLORWRITEENABLE, 0 });

		setTempTextureStage(0, srcResource, srcRect, D3DTEXF_POINT);
		state.setTempTextureStageState({ 0, D3DDDITSS_SRGBTEXTURE, FALSE });

		state.setTempStreamSourceUm({ 0, sizeof(Vertex) }, m_vertices.data());

		DeviceState::TempStateLock lock(state);
		drawRect(srcRect, Rect::toRectF(dstRect), srcSurface.Width, srcSurface.Height);
		m_device.flushPrimitives();
	}

	void ShaderBlitter::displayBlt(const Resource& dstResource, UINT dstSubResourceIndex, const RECT& dstRect,
		const Resource& srcResource, UINT srcSubResourceIndex, const RECT& srcRect)
	{
		auto& repo = m_device.getRepo();
		const bool useGamma = !g_isGammaRampDefault && repo.getGammaRampTexture();

		if (Rect::isEqualSize(dstRect, srcRect))
		{
			if (useGamma)
			{
				gammaBlt(dstResource, dstSubResourceIndex, dstRect, srcResource, srcSubResourceIndex, srcRect);
				return;
			}

			D3DDDIARG_BLT data = {};
			data.hSrcResource = srcResource;
			data.SrcSubResourceIndex = srcSubResourceIndex;
			data.SrcRect = srcRect;
			data.hDstResource = dstResource;
			data.DstSubResourceIndex = dstSubResourceIndex;
			data.DstRect = dstRect;
			data.Flags.Point = 1;
			m_device.getOrigVtable().pfnBlt(m_device, &data);
			return;
		}

		RECT r = { 0, 0, dstRect.right - dstRect.left, dstRect.bottom - dstRect.top };
		Resource* rtGamma = nullptr;
		if (useGamma)
		{
			rtGamma = repo.getNextRenderTarget(r.right, r.bottom, &srcResource).resource;
		}

		const auto& rt = rtGamma ? *rtGamma : dstResource;
		const auto rtIndex = rtGamma ? 0 : dstSubResourceIndex;
		const auto& rtRect = rtGamma ? r : dstRect;

		switch (Config::displayFilter.get())
		{
		case Config::Settings::DisplayFilter::POINT:
			m_device.getShaderBlitter().textureBlt(rt, rtIndex, rtRect,
				srcResource, srcSubResourceIndex, srcRect, D3DTEXF_POINT);
			break;

		case Config::Settings::DisplayFilter::BILINEAR:
			m_device.getShaderBlitter().genBilinearBlt(rt, rtIndex, rtRect,
				srcResource, srcSubResourceIndex, srcRect, Config::displayFilter.getParam());
			break;

		case Config::Settings::DisplayFilter::BICUBIC:
			m_device.getShaderBlitter().bicubicBlt(rt, rtIndex, rtRect,
				srcResource, srcSubResourceIndex, srcRect, Config::displayFilter.getParam());
			break;

		case Config::Settings::DisplayFilter::LANCZOS:
			m_device.getShaderBlitter().lanczosBlt(rt, rtIndex, rtRect,
				srcResource, srcSubResourceIndex, srcRect, Config::displayFilter.getParam());
			break;
		}

		if (rtGamma)
		{
			gammaBlt(dstResource, dstSubResourceIndex, dstRect, rt, rtIndex, rtRect);
		}
	}

	void ShaderBlitter::drawRect(const RECT& srcRect, const RectF& dstRect, UINT srcWidth, UINT srcHeight)
	{
		m_vertices[0].xy = { dstRect.left - 0.5f, dstRect.top - 0.5f };
		m_vertices[1].xy = { dstRect.right - 0.5f, dstRect.top - 0.5f };
		m_vertices[2].xy = { dstRect.left - 0.5f, dstRect.bottom - 0.5f };
		m_vertices[3].xy = { dstRect.right - 0.5f, dstRect.bottom - 0.5f };

		setTextureCoords(0, srcRect, srcWidth, srcHeight);

		D3DDDIARG_DRAWPRIMITIVE dp = {};
		dp.PrimitiveType = D3DPT_TRIANGLESTRIP;
		dp.VStart = 0;
		dp.PrimitiveCount = 2;
		m_device.pfnDrawPrimitive(&dp, nullptr);
	}

	void ShaderBlitter::gammaBlt(const Resource& dstResource, UINT dstSubResourceIndex, const RECT& dstRect,
		const Resource& srcResource, UINT srcSubResourceIndex, const RECT& srcRect)
	{
		LOG_FUNC("ShaderBlitter::gammaBlt", static_cast<HANDLE>(dstResource), dstSubResourceIndex, dstRect,
			static_cast<HANDLE>(srcResource), srcSubResourceIndex, srcRect);

		auto gammaRampTexture(m_device.getRepo().getGammaRampTexture());
		if (!gammaRampTexture)
		{
			return;
		}

		if (g_isGammaRampInvalidated)
		{
			D3DDDIARG_LOCK lock = {};
			lock.hResource = *gammaRampTexture;
			lock.Flags.Discard = 1;
			m_device.getOrigVtable().pfnLock(m_device, &lock);
			if (!lock.pSurfData)
			{
				return;
			}

			auto ptr = static_cast<BYTE*>(lock.pSurfData);
			setGammaValues(ptr, g_gammaRamp.Red);
			setGammaValues(ptr + lock.Pitch, g_gammaRamp.Green);
			setGammaValues(ptr + 2 * lock.Pitch, g_gammaRamp.Blue);

			D3DDDIARG_UNLOCK unlock = {};
			unlock.hResource = *gammaRampTexture;
			m_device.getOrigVtable().pfnUnlock(m_device, &unlock);
			g_isGammaRampInvalidated = false;
		}

		setTempTextureStage(1, *gammaRampTexture, srcRect, D3DTEXF_POINT);
		blt(dstResource, dstSubResourceIndex, dstRect, srcResource, 0, srcRect, m_psGamma.get(), D3DTEXF_POINT);
	}

	void ShaderBlitter::genBilinearBlt(const Resource& dstResource, UINT dstSubResourceIndex, const RECT& dstRect,
		const Resource& srcResource, UINT srcSubResourceIndex, const RECT& srcRect, UINT blurPercent)
	{
		LOG_FUNC("ShaderBlitter::genBilinearBlt", static_cast<HANDLE>(dstResource), dstSubResourceIndex, dstRect,
			static_cast<HANDLE>(srcResource), srcSubResourceIndex, srcRect, blurPercent);
		if (100 == blurPercent)
		{
			blt(dstResource, dstSubResourceIndex, dstRect, srcResource, srcSubResourceIndex, srcRect,
				m_psTextureSampler.get(), D3DTEXF_LINEAR | D3DTEXF_SRGB);
			return;
		}

		const auto& srcDesc = srcResource.getFixedDesc().pSurfList[0];
		float scaleX = static_cast<float>(dstRect.right - dstRect.left) / (srcRect.right - srcRect.left);
		float scaleY = static_cast<float>(dstRect.bottom - dstRect.top) / (srcRect.bottom - srcRect.top);

		const float blur = blurPercent / 100.0f;
		scaleX = 1 / ((1 - blur) / scaleX + blur);
		scaleY = 1 / ((1 - blur) / scaleY + blur);

		const std::array<DeviceState::ShaderConstF, 2> registers{ {
			{ static_cast<float>(srcDesc.Width), static_cast<float>(srcDesc.Height), 0.0f, 0.0f },
			{ scaleX, scaleY, 0.0f, 0.0f }
		} };

		DeviceState::TempPixelShaderConst tempPsConst(m_device.getState(), { 0, registers.size() }, registers.data());
		blt(dstResource, dstSubResourceIndex, dstRect, srcResource, srcSubResourceIndex, srcRect, m_psGenBilinear.get(),
			D3DTEXF_LINEAR | D3DTEXF_SRGB);
	}

	void ShaderBlitter::lanczosBlt(const Resource& dstResource, UINT dstSubResourceIndex, const RECT& dstRect,
		const Resource& srcResource, UINT srcSubResourceIndex, const RECT& srcRect, UINT lobes)
	{
		LOG_FUNC("ShaderBlitter::lanczosBlt", static_cast<HANDLE>(dstResource), dstSubResourceIndex, dstRect,
			static_cast<HANDLE>(srcResource), srcSubResourceIndex, srcRect, lobes);

		convolutionBlt(dstResource, dstSubResourceIndex, dstRect, srcResource, srcSubResourceIndex, srcRect,
			lobes, m_psLanczos.get());
	}

	void ShaderBlitter::lockRefBlt(const Resource& dstResource, UINT dstSubResourceIndex, const RECT& dstRect,
		const Resource& srcResource, UINT srcSubResourceIndex, const RECT& srcRect,
		const Resource& lockRefResource)
	{
		LOG_FUNC("ShaderBlitter::lockRefBlt", static_cast<HANDLE>(dstResource), dstSubResourceIndex, dstRect,
			static_cast<HANDLE>(srcResource), srcSubResourceIndex, srcRect,
			static_cast<HANDLE>(lockRefResource));

		setTempTextureStage(1, lockRefResource, srcRect, D3DTEXF_POINT);
		blt(dstResource, dstSubResourceIndex, dstRect, srcResource, srcSubResourceIndex, srcRect,
			m_psLockRef.get(), D3DTEXF_POINT);
	}

	void ShaderBlitter::palettizedBlt(const Resource& dstResource, UINT dstSubResourceIndex, const RECT& dstRect,
		const Resource& srcResource, UINT srcSubResourceIndex, const RECT& srcRect, RGBQUAD palette[256])
	{
		LOG_FUNC("ShaderBlitter::palettizedBlt", static_cast<HANDLE>(dstResource), dstSubResourceIndex, dstRect,
			static_cast<HANDLE>(srcResource), srcSubResourceIndex, srcRect,
			Compat::array(reinterpret_cast<void**>(palette), 256));

		auto paletteTexture(m_device.getRepo().getPaletteTexture());
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

		setTempTextureStage(1, *paletteTexture, srcRect, D3DTEXF_POINT);
		blt(dstResource, dstSubResourceIndex, dstRect, srcResource, srcSubResourceIndex, srcRect,
			m_psPaletteLookup.get(), D3DTEXF_POINT);
	}

	void ShaderBlitter::resetGammaRamp()
	{
		g_isGammaRampDefault = true;
		g_isGammaRampInvalidated = false;
	}

	void ShaderBlitter::setGammaRamp(const D3DDDI_GAMMA_RAMP_RGB256x3x16& ramp)
	{
		g_gammaRamp = ramp;
		g_isGammaRampDefault = true;
		for (WORD i = 0; i < 256 && g_isGammaRampDefault; ++i)
		{
			const WORD defaultRamp = i * 0xFFFF / 0xFF;
			g_isGammaRampDefault = defaultRamp == ramp.Red[i] && defaultRamp == ramp.Green[i] && defaultRamp == ramp.Blue[i];
		}
		g_isGammaRampInvalidated = !g_isGammaRampDefault;
	}

	void ShaderBlitter::setTempTextureStage(UINT stage, const Resource& texture, const RECT& rect, UINT filter)
	{
		auto& state = m_device.getState();
		state.setTempTexture(stage, texture);
		state.setTempTextureStageState({ stage, D3DDDITSS_TEXCOORDINDEX, stage });
		state.setTempTextureStageState({ stage, D3DDDITSS_ADDRESSU, D3DTADDRESS_CLAMP });
		state.setTempTextureStageState({ stage, D3DDDITSS_ADDRESSV, D3DTADDRESS_CLAMP });
		state.setTempTextureStageState({ stage, D3DDDITSS_MAGFILTER, filter });
		state.setTempTextureStageState({ stage, D3DDDITSS_MINFILTER, filter });
		state.setTempTextureStageState({ stage, D3DDDITSS_MIPFILTER, D3DTEXF_NONE });
		state.setTempRenderState({ static_cast<D3DDDIRENDERSTATETYPE>(D3DDDIRS_WRAP0 + stage), 0 });

		auto& si = texture.getFixedDesc().pSurfList[0];
		setTextureCoords(stage, rect, si.Width, si.Height);
	}

	void ShaderBlitter::setTextureCoords(UINT stage, const RECT& rect, UINT width, UINT height)
	{
		const float w = static_cast<float>(width);
		const float h = static_cast<float>(height);
		m_vertices[0].tc[stage] = { rect.left / w, rect.top / h };
		m_vertices[1].tc[stage] = { rect.right / w, rect.top / h };
		m_vertices[2].tc[stage] = { rect.left / w, rect.bottom / h };
		m_vertices[3].tc[stage] = { rect.right / w, rect.bottom / h };
	}

	void ShaderBlitter::textureBlt(const Resource& dstResource, UINT dstSubResourceIndex, const RECT& dstRect,
		const Resource& srcResource, UINT srcSubResourceIndex, const RECT& srcRect,
		UINT filter, const DeviceState::ShaderConstF* srcColorKey, const BYTE* alpha, const Gdi::Region& srcRgn)
	{
		if (srcColorKey)
		{
			DeviceState::TempPixelShaderConst psConst(m_device.getState(), { 31, 1 }, srcColorKey);
			blt(dstResource, dstSubResourceIndex, dstRect, srcResource, srcSubResourceIndex, srcRect,
				m_psColorKey.get(), filter, 0, alpha, srcRgn);
		}
		else
		{
			blt(dstResource, dstSubResourceIndex, dstRect, srcResource, srcSubResourceIndex, srcRect,
				m_psTextureSampler.get(), filter, 0, alpha, srcRgn);
		}
	}
}
