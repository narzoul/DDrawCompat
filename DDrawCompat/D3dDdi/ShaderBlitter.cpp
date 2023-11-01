#include <Common/Log.h>
#include <Common/Rect.h>
#include <Config/Settings/DisplayFilter.h>
#include <D3dDdi/Adapter.h>
#include <D3dDdi/Device.h>
#include <D3dDdi/Resource.h>
#include <D3dDdi/ShaderBlitter.h>
#include <D3dDdi/SurfaceRepository.h>
#include <DDraw/Surfaces/PrimarySurface.h>
#include <Shaders/Bilinear.h>
#include <Shaders/ColorKey.h>
#include <Shaders/ColorKeyBlend.h>
#include <Shaders/CubicConvolution2.h>
#include <Shaders/CubicConvolution3.h>
#include <Shaders/CubicConvolution4.h>
#include <Shaders/DepthBlt.h>
#include <Shaders/DrawCursor.h>
#include <Shaders/Lanczos.h>
#include <Shaders/LockRef.h>
#include <Shaders/PaletteLookup.h>
#include <Shaders/Point.h>
#include <Shaders/PointNoFilter.h>
#include <Shaders/TextureSampler.h>

#define CONCAT_(a, b) a##b
#define CONCAT(a, b) CONCAT_(a, b)
#define SCOPED_STATE(state, ...) DeviceState::Scoped##state CONCAT(scopedState, __LINE__)(m_device.getState(), __VA_ARGS__)

namespace
{
	const UINT CF_HORIZONTAL = 1;
	const UINT CF_GAMMARAMP = 2;
	const UINT CF_DITHERING = 4;

	D3DDDI_GAMMA_RAMP_RGB256x3x16 g_gammaRamp;
	bool g_isGammaRampDefault = true;
	bool g_isGammaRampInvalidated = false;

	std::array<D3dDdi::DeviceState::ShaderConstF, 2> convertToShaderConst(D3dDdi::ShaderBlitter::ColorKeyInfo colorKeyInfo)
	{
		const auto& fi = D3dDdi::getFormatInfo(colorKeyInfo.format);
		return { {
			{
				D3dDdi::getComponentAsFloat(colorKeyInfo.colorKey, fi.red),
				D3dDdi::getComponentAsFloat(colorKeyInfo.colorKey, fi.green),
				D3dDdi::getComponentAsFloat(colorKeyInfo.colorKey, fi.blue),
				0
			},
			{
				0.5f / ((1 << fi.red.bitCount) - 1),
				0.5f / ((1 << fi.green.bitCount) - 1),
				0.5f / ((1 << fi.blue.bitCount) - 1),
			}
		} };
	}

	constexpr D3dDdi::DeviceState::ShaderConstF getSplineWeights(int n, float a, float b, float c, float d)
	{
		return {
			a,
			-3 * n * a + b,
			3 * n * n * a - 2 * n * b + c,
			-n * n * n * a + n * n * b - n * c + d
		};
	}

	void setGammaValues(BYTE* ptr, USHORT* ramp)
	{
		for (UINT i = 0; i < 256; ++i)
		{
			ptr[i] = static_cast<BYTE>(ramp[i] / 256);
		}
	}
}

namespace D3dDdi
{
	ShaderBlitter::ShaderBlitter(Device& device)
		: m_device(device)
		, m_psBilinear(createPixelShader(g_psBilinear))
		, m_psColorKey(createPixelShader(g_psColorKey))
		, m_psColorKeyBlend(createPixelShader(g_psColorKeyBlend))
		, m_psCubicConvolution{
			createPixelShader(g_psCubicConvolution2),
			createPixelShader(g_psCubicConvolution3),
			createPixelShader(g_psCubicConvolution4)
		}
		, m_psDepthBlt(createPixelShader(g_psDepthBlt))
		, m_psDrawCursor(createPixelShader(g_psDrawCursor))
		, m_psLanczos(createPixelShader(g_psLanczos))
		, m_psLockRef(createPixelShader(g_psLockRef))
		, m_psPaletteLookup(createPixelShader(g_psPaletteLookup))
		, m_psPoint(createPixelShader(g_psPoint))
		, m_psPointNoFilter(createPixelShader(g_psPointNoFilter))
		, m_psTextureSampler(createPixelShader(g_psTextureSampler))
		, m_vertexShaderDecl(createVertexShaderDecl())
		, m_convolutionParams{}
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

		m_convolutionParams.extra[0] = { (12 - 9 * B - 6 * C) / 6, (-18 + 12 * B + 6 * C) / 6, 0, (6 - 2 * B) / 6 };
		m_convolutionParams.extra[1] = { (-B - 6 * C) / 6, (6 * B + 30 * C) / 6, (-12 * B - 48 * C) / 6, (8 * B + 24 * C) / 6 };

		convolutionBlt(dstResource, dstSubResourceIndex, dstRect, srcResource, srcSubResourceIndex, srcRect,
			2, m_psCubicConvolution[0].get());
	}

	void ShaderBlitter::bilinearBlt(const Resource& dstResource, UINT dstSubResourceIndex, const RECT& dstRect,
		const Resource& srcResource, UINT srcSubResourceIndex, const RECT& srcRect, UINT blurPercent)
	{
		LOG_FUNC("ShaderBlitter::bilinearBlt", static_cast<HANDLE>(dstResource), dstSubResourceIndex, dstRect,
			static_cast<HANDLE>(srcResource), srcSubResourceIndex, srcRect, blurPercent);

		const Float2 dstSize(dstRect.right - dstRect.left, dstRect.bottom - dstRect.top);
		const Float2 srcSize(srcRect.right - srcRect.left, srcRect.bottom - srcRect.top);
		const Float2 scale = dstSize / srcSize;
		const Float2 higherScale = max(scale, 1.0f / scale);
		const Float2 blur = Config::displayFilter.getParam() / 100.0f;
		const Float2 adjustedScale = 1.0f / (blur + (1.0f - blur) / higherScale);
		const Float2 multiplier = -1.0f * adjustedScale;
		const Float2 offset = 0.5f * adjustedScale + 0.5f;
		const Float2 support = 0.5f + 0.5f / adjustedScale;

		convolutionBlt(dstResource, dstSubResourceIndex, dstRect, srcResource, srcSubResourceIndex, srcRect,
			support, m_psBilinear.get(), [&](bool isHorizontal) {
				if (isHorizontal)
				{
					m_convolutionParams.extra[0] = { multiplier.x, multiplier.y, offset.x, offset.y };
				}
				else
				{
					m_convolutionParams.extra[0] = { multiplier.y, multiplier.x, offset.y, offset.x };
				}
			});
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

		bool srgbRead = (filter & D3DTEXF_SRGBREAD) && (srcResource.getFormatOp().Operations & FORMATOP_SRGBREAD);
		bool srgbWrite = (filter & D3DTEXF_SRGBWRITE) && (dstResource.getFormatOp().Operations & FORMATOP_SRGBWRITE);
		if (D3DTEXF_SRGB == HIWORD(filter) && (!srgbRead || !srgbWrite))
		{
			srgbRead = false;
			srgbWrite = false;
		}

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
		state.setTempRenderState({ D3DDDIRS_SRGBWRITEENABLE, srgbWrite });

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

		setTempTextureStage(0, srcResource, srcRect, LOWORD(filter) | (srgbRead ? D3DTEXF_SRGBREAD : 0));

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
		const Resource& srcResource, UINT srcSubResourceIndex, ColorKeyInfo srcColorKey)
	{
		const auto ck = convertToShaderConst(srcColorKey);
		DeviceState::TempPixelShaderConst psConst(m_device.getState(), { 30, 2 }, ck.data());
		blt(dstResource, dstSubResourceIndex, dstResource.getRect(dstSubResourceIndex),
			srcResource, srcSubResourceIndex, srcResource.getRect(srcSubResourceIndex),
			m_psColorKeyBlend.get(), D3DTEXF_POINT);
	}

	void ShaderBlitter::convolution(const Resource& dstResource, UINT dstSubResourceIndex, const RECT& dstRect,
		const Resource& srcResource, UINT srcSubResourceIndex, const RECT& srcRect,
		Float2 support, HANDLE pixelShader, const std::function<void(bool)> setExtraParams, DWORD flags)
	{
		LOG_FUNC("ShaderBlitter::convolution", static_cast<HANDLE>(dstResource), dstSubResourceIndex, dstRect,
			static_cast<HANDLE>(srcResource), srcSubResourceIndex, srcRect,
			support, pixelShader, static_cast<bool>(setExtraParams), flags);

		const auto& srcDesc = srcResource.getFixedDesc().pSurfList[0];
		const Float2 dstSize(dstRect.right - dstRect.left, dstRect.bottom - dstRect.top);
		const Float2 srcSize(srcRect.right - srcRect.left, srcRect.bottom - srcRect.top);
		const Float2 scale = dstSize / srcSize;

		const bool isHorizontal = flags & CF_HORIZONTAL;
		const bool isDual = srcSize.x != dstSize.x && srcSize.y != dstSize.y;
		const Float2 compMaskPri = isHorizontal ? Float2(1, 0) : Float2(0, 1);
		const Float2 compMaskSec = isHorizontal ? Float2(0, 1) : Float2(1, 0);
		const Float2 compMask = isDual ? Float2(1, 1) : compMaskPri;

		auto& p = m_convolutionParams;
		p.textureSize = { srcDesc.Width, srcDesc.Height };
		p.sampleCoordOffset = -0.5f * compMask;
		p.textureCoordStep = 1.0f / p.textureSize;
		p.kernelCoordStep = min(scale, 1.0f);
		p.textureCoordStepPri = 2.0f * p.textureCoordStep * compMaskPri;
		p.textureCoordStepSec = p.textureCoordStep * compMaskSec;
		p.kernelCoordStepPri = 2.0f * p.kernelCoordStep * compMaskPri;
		p.support = dot(support, compMaskPri);
		p.supportRcp = 1.0f / p.support;

		const Int2 sampleCountHalf = min(ceil(support / p.kernelCoordStep), 255.0f);
		const Float2 firstSampleOffset = 1 - sampleCountHalf;
		p.kernelCoordOffset[0] = firstSampleOffset * p.kernelCoordStep;
		p.kernelCoordOffset[1] = p.kernelCoordOffset[0] + p.kernelCoordStep;
		p.textureCoordOffset[0] = (firstSampleOffset * compMaskPri + 0.5f) * p.textureCoordStep;
		p.textureCoordOffset[1] = p.textureCoordOffset[0] + p.textureCoordStep * compMaskPri;

		if (!isHorizontal)
		{
			std::swap(p.kernelCoordStepPri.x, p.kernelCoordStepPri.y);
		}

		if (setExtraParams)
		{
			setExtraParams(isHorizontal);
		}

		struct BoolParams
		{
			BOOL useSrgbRead;
			BOOL useSrgbReadNeg;
			BOOL useSrgbWrite;
			BOOL useGammaRamp;
			BOOL useDithering;
		};

		BoolParams boolParams = {};
		boolParams.useSrgbRead = !(srcResource.getFormatOp().Operations & FORMATOP_SRGBREAD);
		boolParams.useSrgbReadNeg = !boolParams.useSrgbRead;
		boolParams.useSrgbWrite = (flags & (CF_GAMMARAMP | CF_DITHERING)) ||
			!(dstResource.getFormatOp().Operations & FORMATOP_SRGBWRITE);
		boolParams.useGammaRamp = flags & CF_GAMMARAMP;
		boolParams.useDithering = flags & CF_DITHERING;

		DeviceState::TempPixelShaderConstB tempPsConstB(m_device.getState(), { 0, 5 }, &boolParams.useSrgbRead);

		const DeviceState::ShaderConstI reg = { dot(sampleCountHalf - 1, Int2(compMaskPri)) };
		DeviceState::TempPixelShaderConstI tempPsConstI(m_device.getState(), { 0, 1 }, &reg);

		DeviceState::TempPixelShaderConst tempPsConst(m_device.getState(),
			{ 0, sizeof(m_convolutionParams) / sizeof(DeviceState::ShaderConstF) },
			reinterpret_cast<DeviceState::ShaderConstF*>(&m_convolutionParams));

		UINT filter = (p.support <= 1 && !boolParams.useSrgbRead) ? D3DTEXF_LINEAR : D3DTEXF_POINT;
		if (!boolParams.useSrgbRead && 0 != support.x)
		{
			filter |= D3DTEXF_SRGBREAD;
		}
		if (!boolParams.useSrgbWrite && 0 != support.x)
		{
			filter |= D3DTEXF_SRGBWRITE;
		}

		blt(dstResource, dstSubResourceIndex, dstRect, srcResource, srcSubResourceIndex, srcRect, pixelShader, filter);
	}

	void ShaderBlitter::convolutionBlt(const Resource& dstResource, UINT dstSubResourceIndex, const RECT& dstRect,
		const Resource& srcResource, UINT srcSubResourceIndex, const RECT& srcRect,
		Float2 support, HANDLE pixelShader, const std::function<void(bool)> setExtraParams)
	{
		LOG_FUNC("ShaderBlitter::convolutionBlt", static_cast<HANDLE>(dstResource), dstSubResourceIndex, dstRect,
			static_cast<HANDLE>(srcResource), srcSubResourceIndex, srcRect, support, pixelShader,
			static_cast<bool>(setExtraParams));

		const Int2 dstSize(dstRect.right - dstRect.left, dstRect.bottom - dstRect.top);
		const Int2 srcSize(srcRect.right - srcRect.left, srcRect.bottom - srcRect.top);
		const Float2 scale = Float2(dstSize) / Float2(srcSize);
		const Float2 kernelCoordStep = min(scale, 1.0f);
		const Float2 sampleCountHalf = support / kernelCoordStep;

		DWORD flags = 0;
		Resource* gammaRampTexture = nullptr;
		if (!g_isGammaRampDefault && nullptr != (gammaRampTexture = m_device.getRepo().getGammaRampTexture()))
		{
			flags |= CF_GAMMARAMP;
		}

		const auto& dstFi = getFormatInfo(m_device.getAdapter().getRenderColorDepthDstFormat());
		const auto& srcFi = getFormatInfo(srcResource.getFixedDesc().Format);
		BYTE maxBpcDiff = 0;
		maxBpcDiff = std::max<BYTE>(maxBpcDiff, srcFi.red.bitCount - dstFi.red.bitCount);
		maxBpcDiff = std::max<BYTE>(maxBpcDiff, srcFi.green.bitCount - dstFi.green.bitCount);
		maxBpcDiff = std::max<BYTE>(maxBpcDiff, srcFi.blue.bitCount - dstFi.blue.bitCount);

		Resource* ditherTexture = nullptr;
		DWORD ditherSize = std::min(1 << maxBpcDiff, 16);
		if (0 != maxBpcDiff && nullptr != (ditherTexture = m_device.getRepo().getDitherTexture(ditherSize)))
		{
			flags |= CF_DITHERING;
		}

		if ((flags & CF_GAMMARAMP) && g_isGammaRampInvalidated)
		{
			D3DDDIARG_LOCK lock = {};
			lock.hResource = *gammaRampTexture;
			lock.Flags.Discard = 1;
			m_device.getOrigVtable().pfnLock(m_device, &lock);
			if (lock.pSurfData)
			{
				auto ptr = static_cast<BYTE*>(lock.pSurfData);
				setGammaValues(ptr, g_gammaRamp.Red);
				setGammaValues(ptr + lock.Pitch, g_gammaRamp.Green);
				setGammaValues(ptr + 2 * lock.Pitch, g_gammaRamp.Blue);

				D3DDDIARG_UNLOCK unlock = {};
				unlock.hResource = *gammaRampTexture;
				m_device.getOrigVtable().pfnUnlock(m_device, &unlock);
				g_isGammaRampInvalidated = false;
			}
			else
			{
				flags &= ~CF_GAMMARAMP;
			}
		}

		if (flags & CF_GAMMARAMP)
		{
			setTempTextureStage(1, *gammaRampTexture, srcRect, D3DTEXF_LINEAR);
		}

		if (flags & CF_DITHERING)
		{
			setTempTextureStage(2, *ditherTexture, dstRect, D3DTEXF_POINT, D3DTADDRESS_WRAP);
			m_convolutionParams.maxRgb[0] = static_cast<float>(1 << dstFi.red.bitCount) - 1;
			m_convolutionParams.maxRgb[1] = static_cast<float>(1 << dstFi.green.bitCount) - 1;
			m_convolutionParams.maxRgb[2] = static_cast<float>(1 << dstFi.blue.bitCount) - 1;
			for (unsigned i = 0; i < 3; ++i)
			{
				m_convolutionParams.maxRgbRcp[i] = 1.0f / m_convolutionParams.maxRgb[i];
			}

			const float d = static_cast<float>(ditherSize * ditherSize);
			m_convolutionParams.ditherScale = (d - 1) / d;
			m_convolutionParams.ditherOffset = 0.5f / d;
		}

		if (srcSize.y == dstSize.y ||
			0 == support.y ||
			sampleCountHalf.y <= 1 && (srcResource.getFormatOp().Operations & FORMATOP_SRGBREAD))
		{
			return convolution(dstResource, dstSubResourceIndex, dstRect, srcResource, srcSubResourceIndex, srcRect,
				support, pixelShader, setExtraParams, flags | CF_HORIZONTAL);
		}
		if (srcSize.x == dstSize.x ||
			sampleCountHalf.x <= 1 && (srcResource.getFormatOp().Operations & FORMATOP_SRGBREAD))
		{
			return convolution(dstResource, dstSubResourceIndex, dstRect, srcResource, srcSubResourceIndex, srcRect,
				support, pixelShader, setExtraParams, flags);
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

		auto rt = m_device.getRepo().getNextRenderTarget(rect.right, rect.bottom,
			srcResource.getFixedDesc().Format, &srcResource, &dstResource).resource;
		if (!rt)
		{
			return;
		}

		convolution(*rt, 0, rect, srcResource, srcSubResourceIndex, srcRect,
			support, pixelShader, setExtraParams, isHorizontalFirst ? CF_HORIZONTAL : 0);
		convolution(dstResource, dstSubResourceIndex, dstRect, *rt, 0, rect,
			support, pixelShader, setExtraParams, flags | (isHorizontalFirst ? 0 : CF_HORIZONTAL));
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
			cur.tempTexture->copySubResourceRegion(0, clippedSrcRect, dstResource, dstSubResourceIndex, clippedDstRect);
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

	void ShaderBlitter::displayBlt(Resource& dstResource, UINT dstSubResourceIndex, const RECT& dstRect,
		const Resource& srcResource, UINT srcSubResourceIndex, const RECT& srcRect)
	{
		auto filter = Config::displayFilter.get();
		if (Rect::isEqualSize(dstRect, srcRect))
		{
			filter = Config::Settings::DisplayFilter::POINT;
		}

		switch (filter)
		{
		case Config::Settings::DisplayFilter::POINT:
			m_device.getShaderBlitter().pointBlt(dstResource, dstSubResourceIndex, dstRect,
				srcResource, srcSubResourceIndex, srcRect);
			break;

		case Config::Settings::DisplayFilter::BILINEAR:
			m_device.getShaderBlitter().bilinearBlt(dstResource, dstSubResourceIndex, dstRect,
				srcResource, srcSubResourceIndex, srcRect, Config::displayFilter.getParam());
			break;

		case Config::Settings::DisplayFilter::BICUBIC:
			m_device.getShaderBlitter().bicubicBlt(dstResource, dstSubResourceIndex, dstRect,
				srcResource, srcSubResourceIndex, srcRect, Config::displayFilter.getParam());
			break;

		case Config::Settings::DisplayFilter::LANCZOS:
			m_device.getShaderBlitter().lanczosBlt(dstResource, dstSubResourceIndex, dstRect,
				srcResource, srcSubResourceIndex, srcRect, Config::displayFilter.getParam());
			break;

		case Config::Settings::DisplayFilter::SPLINE:
			m_device.getShaderBlitter().splineBlt(dstResource, dstSubResourceIndex, dstRect,
				srcResource, srcSubResourceIndex, srcRect, Config::displayFilter.getParam());
			break;
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

	void ShaderBlitter::pointBlt(const Resource& dstResource, UINT dstSubResourceIndex, const RECT& dstRect,
		const Resource& srcResource, UINT srcSubResourceIndex, const RECT& srcRect)
	{
		LOG_FUNC("ShaderBlitter::pointBlt", static_cast<HANDLE>(dstResource), dstSubResourceIndex, dstRect,
			static_cast<HANDLE>(srcResource), srcSubResourceIndex, srcRect);

		auto dstSize = Rect::getSize(dstRect);
		auto srcSize = Rect::getSize(srcRect);
		if (dstSize.cx >= srcSize.cx && dstSize.cy >= srcSize.cy)
		{
			convolutionBlt(dstResource, dstSubResourceIndex, dstRect, srcResource, srcSubResourceIndex, srcRect,
				0.0f, m_psPointNoFilter.get());
		}
		else
		{
			convolutionBlt(dstResource, dstSubResourceIndex, dstRect, srcResource, srcSubResourceIndex, srcRect,
				0.5f, m_psPoint.get());
		}
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
			g_isGammaRampDefault = i == ramp.Red[i] / 256 && i == ramp.Green[i] / 256 && i == ramp.Blue[i] / 256;
		}
		g_isGammaRampInvalidated = !g_isGammaRampDefault;
	}

	void ShaderBlitter::setTempTextureStage(UINT stage, const Resource& texture, const RECT& rect, UINT filter,
		UINT textureAddress)
	{
		auto& state = m_device.getState();
		state.setTempTexture(stage, texture);
		state.setTempTextureStageState({ stage, D3DDDITSS_TEXCOORDINDEX, stage });
		state.setTempTextureStageState({ stage, D3DDDITSS_ADDRESSU, textureAddress });
		state.setTempTextureStageState({ stage, D3DDDITSS_ADDRESSV, textureAddress });
		state.setTempTextureStageState({ stage, D3DDDITSS_MAGFILTER, LOWORD(filter) });
		state.setTempTextureStageState({ stage, D3DDDITSS_MINFILTER, LOWORD(filter) });
		state.setTempTextureStageState({ stage, D3DDDITSS_MIPFILTER, D3DTEXF_NONE });
		state.setTempTextureStageState({ stage, D3DDDITSS_SRGBTEXTURE, 0 != (filter & D3DTEXF_SRGBREAD) });
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

	void ShaderBlitter::splineBlt(const Resource& dstResource, UINT dstSubResourceIndex, const RECT& dstRect,
		const Resource& srcResource, UINT srcSubResourceIndex, const RECT& srcRect, UINT lobes)
	{
		LOG_FUNC("ShaderBlitter::splineBlt", static_cast<HANDLE>(dstResource), dstSubResourceIndex, dstRect,
			static_cast<HANDLE>(srcResource), srcSubResourceIndex, srcRect, lobes);

		switch (lobes)
		{
		case 2:
			m_convolutionParams.extra[0] = getSplineWeights(0, 1.0f, -9.0f / 5.0f, -1.0f / 5.0f, 1.0f);
			m_convolutionParams.extra[1] = getSplineWeights(1, -1.0f / 3.0f, 4.0f / 5.0f, -7.0f / 15.0f, 0.0f);
			break;

		case 3:
			m_convolutionParams.extra[0] = getSplineWeights(0, 13.0f / 11.0f, -453.0f / 209.0f, -3.0f / 209.0f, 1.0f);
			m_convolutionParams.extra[1] = getSplineWeights(1, -6.0f / 11.0f, 270.0f / 209.0f, -156.0f / 209.0f, 0.0f);
			m_convolutionParams.extra[2] = getSplineWeights(2, 1.0f / 11.0f, -45.0f / 209.0f, 26.0f / 209.0f, 0.0f);
			break;

		case 4:
			m_convolutionParams.extra[0] = getSplineWeights(0, 49.0f / 41.0f, -6387.0f / 2911.0f, -3.0f / 2911.0f, 1.0f);
			m_convolutionParams.extra[1] = getSplineWeights(1, -24.0f / 41.0f, 4032.0f / 2911.0f, -2328.0f / 2911.0f, 0.0f);
			m_convolutionParams.extra[2] = getSplineWeights(2, 6.0f / 41.0f, -1008.0f / 2911.0f, 582.0f / 2911.0f, 0.0f);
			m_convolutionParams.extra[3] = getSplineWeights(3, -1.0f / 41.0f, 168.0f / 2911.0f, -97.0f / 2911.0f, 0.0f);
			break;
		}

		convolutionBlt(dstResource, dstSubResourceIndex, dstRect, srcResource, srcSubResourceIndex, srcRect,
			lobes, m_psCubicConvolution[lobes - 2].get());
	}

	void ShaderBlitter::textureBlt(const Resource& dstResource, UINT dstSubResourceIndex, const RECT& dstRect,
		const Resource& srcResource, UINT srcSubResourceIndex, const RECT& srcRect,
		UINT filter, ColorKeyInfo srcColorKey, const BYTE* alpha, const Gdi::Region& srcRgn)
	{
		if (D3DDDIFMT_UNKNOWN != srcColorKey.format)
		{
			const auto ck = convertToShaderConst(srcColorKey);
			DeviceState::TempPixelShaderConst psConst(m_device.getState(), { 30, 2 }, ck.data());
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
