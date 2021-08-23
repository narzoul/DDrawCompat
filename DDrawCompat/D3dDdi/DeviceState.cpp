#include <Config/Config.h>
#include <Common/Log.h>
#include <D3dDdi/Device.h>
#include <D3dDdi/DeviceState.h>
#include <D3dDdi/DrawPrimitive.h>
#include <D3dDdi/Log/DeviceFuncsLog.h>
#include <D3dDdi/Resource.h>

namespace
{
	UINT mapTssValue(D3DDDITEXTURESTAGESTATETYPE type, UINT value)
	{
		switch (type)
		{
		case D3DDDITSS_MAGFILTER:
		case D3DDDITSS_MINFILTER:
			return D3DTEXF_NONE == Config::textureFilter.getFilter() ? value : Config::textureFilter.getFilter();

		case D3DDDITSS_MIPFILTER:
			return D3DTEXF_NONE == Config::textureFilter.getMipFilter() ? value : Config::textureFilter.getMipFilter();

		case D3DDDITSS_MAXANISOTROPY:
			return D3DTEXF_NONE == Config::textureFilter.getFilter() ? value : Config::textureFilter.getMaxAnisotropy();
		}

		return value;
	}
}

namespace D3dDdi
{
	DeviceState::DeviceState(Device& device)
		: m_device(device)
		, m_depthStencil{}
		, m_pixelShader(nullptr)
		, m_renderTarget{}
		, m_streamSource{}
		, m_streamSourceUm{}
		, m_streamSourceUmBuffer(nullptr)
		, m_textures{}
		, m_vertexShaderDecl(nullptr)
		, m_vertexShaderFunc(nullptr)
		, m_viewport{}
		, m_wInfo{}
		, m_zRange{}
	{
		const UINT D3DBLENDOP_ADD = 1;
		const UINT UNINITIALIZED_STATE = 0xBAADBAAD;

		m_device.getOrigVtable().pfnSetDepthStencil(m_device, &m_depthStencil);
		m_device.getOrigVtable().pfnSetPixelShader(m_device, nullptr);
		m_device.getOrigVtable().pfnSetRenderTarget(m_device, &m_renderTarget);
		m_device.getOrigVtable().pfnSetVertexShaderDecl(m_device, nullptr);
		m_device.getOrigVtable().pfnSetVertexShaderFunc(m_device, nullptr);
		m_device.getOrigVtable().pfnSetViewport(m_device, &m_viewport);
		m_device.getOrigVtable().pfnUpdateWInfo(m_device, &m_wInfo);
		m_device.getOrigVtable().pfnSetZRange(m_device, &m_zRange);

		m_renderState.fill(UNINITIALIZED_STATE);
		m_renderState[D3DDDIRS_ZENABLE] = D3DZB_TRUE;
		m_renderState[D3DDDIRS_FILLMODE] = D3DFILL_SOLID;
		m_renderState[D3DDDIRS_SHADEMODE] = D3DSHADE_GOURAUD;
		m_renderState[D3DDDIRS_LINEPATTERN] = 0;
		m_renderState[D3DDDIRS_ZWRITEENABLE] = TRUE;
		m_renderState[D3DDDIRS_ALPHATESTENABLE] = FALSE;
		m_renderState[D3DDDIRS_LASTPIXEL] = TRUE;
		m_renderState[D3DDDIRS_SRCBLEND] = D3DBLEND_ONE;
		m_renderState[D3DDDIRS_DESTBLEND] = D3DBLEND_ZERO;
		m_renderState[D3DDDIRS_CULLMODE] = D3DCULL_CCW;
		m_renderState[D3DDDIRS_ZFUNC] = D3DCMP_LESSEQUAL;
		m_renderState[D3DDDIRS_ALPHAREF] = 0;
		m_renderState[D3DDDIRS_ALPHAFUNC] = D3DCMP_ALWAYS;
		m_renderState[D3DDDIRS_DITHERENABLE] = FALSE;
		m_renderState[D3DDDIRS_ALPHABLENDENABLE] = FALSE;
		m_renderState[D3DDDIRS_FOGENABLE] = FALSE;
		m_renderState[D3DDDIRS_SPECULARENABLE] = FALSE;
		m_renderState[D3DDDIRS_ZVISIBLE] = 0;
		m_renderState[D3DDDIRS_FOGCOLOR] = 0;
		m_renderState[D3DDDIRS_FOGTABLEMODE] = D3DFOG_NONE;
		m_renderState[D3DDDIRS_FOGSTART] = 0;
		m_renderState[D3DDDIRS_FOGEND] = 0x3F800000;
		m_renderState[D3DDDIRS_FOGDENSITY] = 0x3F800000;
		m_renderState[D3DDDIRS_COLORKEYENABLE] = FALSE;
		m_renderState[D3DDDIRS_EDGEANTIALIAS] = 0;
		m_renderState[D3DDDIRS_ZBIAS] = 0;
		m_renderState[D3DDDIRS_RANGEFOGENABLE] = FALSE;
		for (UINT i = D3DDDIRS_STIPPLEPATTERN00; i <= D3DDDIRS_STIPPLEPATTERN31; ++i)
		{
			m_renderState[i] = 0;
		}
		m_renderState[D3DDDIRS_STENCILENABLE] = FALSE;
		m_renderState[D3DDDIRS_STENCILFAIL] = D3DSTENCILOP_KEEP;
		m_renderState[D3DDDIRS_STENCILZFAIL] = D3DSTENCILOP_KEEP;
		m_renderState[D3DDDIRS_STENCILPASS] = D3DSTENCILOP_KEEP;
		m_renderState[D3DDDIRS_STENCILFUNC] = D3DCMP_ALWAYS;
		m_renderState[D3DDDIRS_STENCILREF] = 0;
		m_renderState[D3DDDIRS_STENCILMASK] = 0xFFFFFFFF;
		m_renderState[D3DDDIRS_STENCILWRITEMASK] = 0xFFFFFFFF;
		m_renderState[D3DDDIRS_TEXTUREFACTOR] = 0xFFFFFFFF;
		for (UINT i = D3DDDIRS_WRAP0; i <= D3DDDIRS_WRAP7; ++i)
		{
			m_renderState[i] = 0;
		}
		m_renderState[D3DDDIRS_CLIPPING] = TRUE;
		m_renderState[D3DDDIRS_CLIPPLANEENABLE] = 0;
		m_renderState[D3DDDIRS_SOFTWAREVERTEXPROCESSING] = FALSE;
		m_renderState[D3DDDIRS_POINTSIZE_MAX] = 0x3F800000;
		m_renderState[D3DDDIRS_POINTSIZE] = 0x3F800000;
		m_renderState[D3DDDIRS_POINTSIZE_MIN] = 0;
		m_renderState[D3DDDIRS_POINTSPRITEENABLE] = 0;
		m_renderState[D3DDDIRS_MULTISAMPLEMASK] = 0xFFFFFFFF;
		m_renderState[D3DDDIRS_MULTISAMPLEANTIALIAS] = TRUE;
		m_renderState[D3DDDIRS_PATCHEDGESTYLE] = FALSE;
		m_renderState[D3DDDIRS_PATCHSEGMENTS] = 0x3F800000;
		m_renderState[D3DDDIRS_COLORWRITEENABLE] = 0xF;
		m_renderState[D3DDDIRS_BLENDOP] = D3DBLENDOP_ADD;
		m_renderState[D3DDDIRS_SRGBWRITEENABLE] = FALSE;

		for (UINT i = 0; i < m_renderState.size(); i++)
		{
			if (UNINITIALIZED_STATE != m_renderState[i])
			{
				D3DDDIARG_RENDERSTATE data = {};
				data.State = static_cast<D3DDDIRENDERSTATETYPE>(i);
				data.Value = m_renderState[i];
				m_device.getOrigVtable().pfnSetRenderState(m_device, &data);
			}
		}

		for (UINT i = 0; i < m_textures.size(); ++i)
		{
			m_device.getOrigVtable().pfnSetTexture(m_device, i, nullptr);
		}

		for (UINT i = 0; i < m_textureStageState.size(); ++i)
		{
			m_textureStageState[i].fill(UNINITIALIZED_STATE);
			m_textureStageState[i][D3DDDITSS_TEXCOORDINDEX] = i;
			m_textureStageState[i][D3DDDITSS_ADDRESSU] = D3DTADDRESS_WRAP;
			m_textureStageState[i][D3DDDITSS_ADDRESSV] = D3DTADDRESS_WRAP;
			m_textureStageState[i][D3DDDITSS_BORDERCOLOR] = 0;
			m_textureStageState[i][D3DDDITSS_MAGFILTER] = mapTssValue(D3DDDITSS_MAGFILTER, D3DTEXF_POINT);
			m_textureStageState[i][D3DDDITSS_MINFILTER] = mapTssValue(D3DDDITSS_MINFILTER, D3DTEXF_POINT);
			m_textureStageState[i][D3DDDITSS_MIPFILTER] = mapTssValue(D3DDDITSS_MIPFILTER, D3DTEXF_NONE);
			m_textureStageState[i][D3DDDITSS_MIPMAPLODBIAS] = 0;
			m_textureStageState[i][D3DDDITSS_MAXMIPLEVEL] = 0;
			m_textureStageState[i][D3DDDITSS_MAXANISOTROPY] = mapTssValue(D3DDDITSS_MAXANISOTROPY, 1);
			m_textureStageState[i][D3DDDITSS_TEXTURETRANSFORMFLAGS] = D3DTTFF_DISABLE;
			m_textureStageState[i][D3DDDITSS_SRGBTEXTURE] = FALSE;
			m_textureStageState[i][D3DDDITSS_ADDRESSW] = D3DTADDRESS_WRAP;
			m_textureStageState[i][D3DDDITSS_DISABLETEXTURECOLORKEY] = TRUE;

			for (UINT j = 0; j < m_textureStageState[i].size(); ++j)
			{
				if (UNINITIALIZED_STATE != m_textureStageState[i][j])
				{
					D3DDDIARG_TEXTURESTAGESTATE data = {};
					data.Stage = i;
					data.State = static_cast<D3DDDITEXTURESTAGESTATETYPE>(j);
					data.Value = m_textureStageState[i][j];
					m_device.getOrigVtable().pfnSetTextureStageState(m_device, &data);
				}
			}
		}
	}

	HRESULT DeviceState::pfnCreateVertexShaderDecl(
		D3DDDIARG_CREATEVERTEXSHADERDECL* data,
		const D3DDDIVERTEXELEMENT* vertexElements)
	{
		LOG_DEBUG << Compat::array(vertexElements, data->NumVertexElements);
		HRESULT result = m_device.getOrigVtable().pfnCreateVertexShaderDecl(m_device, data, vertexElements);
		if (SUCCEEDED(result))
		{
			m_vertexShaderDecls[data->ShaderHandle].assign(vertexElements, vertexElements + data->NumVertexElements);
		}
		return result;
	}

	HRESULT DeviceState::pfnDeletePixelShader(HANDLE shader)
	{
		return deleteShader(shader, m_pixelShader, m_device.getOrigVtable().pfnDeletePixelShader);
	}

	HRESULT DeviceState::pfnDeleteVertexShaderDecl(HANDLE shader)
	{
		const bool isCurrentShader = shader == m_vertexShaderDecl;
		HRESULT result = deleteShader(shader, m_vertexShaderDecl, m_device.getOrigVtable().pfnDeleteVertexShaderDecl);
		if (SUCCEEDED(result))
		{
			m_vertexShaderDecls.erase(shader);
			if (isCurrentShader)
			{
				m_device.getDrawPrimitive().setVertexShaderDecl({});
			}
		}
		return result;
	}

	HRESULT DeviceState::pfnDeleteVertexShaderFunc(HANDLE shader)
	{
		return deleteShader(shader, m_vertexShaderFunc, m_device.getOrigVtable().pfnDeleteVertexShaderFunc);
	}

	HRESULT DeviceState::pfnSetDepthStencil(const D3DDDIARG_SETDEPTHSTENCIL* data)
	{
		if (0 == memcmp(data, &m_depthStencil, sizeof(m_depthStencil)))
		{
			return S_OK;
		}

		D3DDDIARG_SETDEPTHSTENCIL d = *data;
		Resource* resource = m_device.getResource(d.hZBuffer);
		if (resource && resource->getCustomResource())
		{
			d.hZBuffer = *resource->getCustomResource();
		}

		m_device.flushPrimitives();
		HRESULT result = m_device.getOrigVtable().pfnSetDepthStencil(m_device, &d);
		if (SUCCEEDED(result))
		{
			m_depthStencil = *data;
		}
		return result;
	}

	HRESULT DeviceState::pfnSetPixelShader(HANDLE shader)
	{
		return setShader(shader, m_pixelShader, m_device.getOrigVtable().pfnSetPixelShader);
	}

	HRESULT DeviceState::pfnSetPixelShaderConst(const D3DDDIARG_SETPIXELSHADERCONST* data, const FLOAT* registers)
	{
		return setShaderConst(data, registers, m_pixelShaderConst, m_device.getOrigVtable().pfnSetPixelShaderConst);
	}

	HRESULT DeviceState::pfnSetPixelShaderConstB(const D3DDDIARG_SETPIXELSHADERCONSTB* data, const BOOL* registers)
	{
		return setShaderConst(data, registers, m_pixelShaderConstB, m_device.getOrigVtable().pfnSetPixelShaderConstB);
	}

	HRESULT DeviceState::pfnSetPixelShaderConstI(const D3DDDIARG_SETPIXELSHADERCONSTI* data, const INT* registers)
	{
		return setShaderConst(data, registers, m_pixelShaderConstI, m_device.getOrigVtable().pfnSetPixelShaderConstI);
	}

	HRESULT DeviceState::pfnSetRenderState(const D3DDDIARG_RENDERSTATE* data)
	{
		if (D3DDDIRS_MULTISAMPLEANTIALIAS == data->State)
		{
			return S_OK;
		}
		if (data->State >= D3DDDIRS_WRAP0 && data->State <= D3DDDIRS_WRAP7)
		{
			auto d = *data;
			d.Value &= D3DWRAPCOORD_0 | D3DWRAPCOORD_1 | D3DWRAPCOORD_2 | D3DWRAPCOORD_3;
			return setStateArray(&d, m_renderState, m_device.getOrigVtable().pfnSetRenderState);
		}
		return setStateArray(data, m_renderState, m_device.getOrigVtable().pfnSetRenderState);
	}

	HRESULT DeviceState::pfnSetRenderTarget(const D3DDDIARG_SETRENDERTARGET* data)
	{
		if (0 == memcmp(data, &m_renderTarget, sizeof(m_renderTarget)))
		{
			return S_OK;
		}

		D3DDDIARG_SETRENDERTARGET d = *data;
		Resource* resource = m_device.getResource(d.hRenderTarget);
		if (resource && resource->getCustomResource())
		{
			d.hRenderTarget = *resource->getCustomResource();
		}

		m_device.flushPrimitives();
		HRESULT result = m_device.getOrigVtable().pfnSetRenderTarget(m_device, &d);
		if (SUCCEEDED(result))
		{
			m_renderTarget = *data;
			m_device.setRenderTarget(*data);
		}
		return result;
	}

	HRESULT DeviceState::pfnSetStreamSource(const D3DDDIARG_SETSTREAMSOURCE* data)
	{
		HRESULT result = m_device.getDrawPrimitive().setStreamSource(*data);
		if (SUCCEEDED(result))
		{
			m_streamSource = *data;
			m_streamSourceUm = {};
			m_streamSourceUmBuffer = nullptr;
		}
		return result;
	}

	HRESULT DeviceState::pfnSetStreamSourceUm(const D3DDDIARG_SETSTREAMSOURCEUM* data, const void* umBuffer)
	{
		HRESULT result = m_device.getDrawPrimitive().setStreamSourceUm(*data, umBuffer);
		if (SUCCEEDED(result))
		{
			m_streamSourceUm = *data;
			m_streamSourceUmBuffer = umBuffer;
			m_streamSource = {};
		}
		return result;
	}

	HRESULT DeviceState::pfnSetTexture(UINT stage, HANDLE texture)
	{
		if (stage >= m_textures.size())
		{
			m_device.flushPrimitives();
			return m_device.getOrigVtable().pfnSetTexture(m_device, stage, texture);
		}

		if (texture == m_textures[stage])
		{
			return S_OK;
		}

		m_device.flushPrimitives();
		HRESULT result = m_device.getOrigVtable().pfnSetTexture(m_device, stage, texture);
		if (SUCCEEDED(result))
		{
			m_textures[stage] = texture;
			m_textureStageState[stage][D3DDDITSS_DISABLETEXTURECOLORKEY] = TRUE;

			D3DDDIARG_TEXTURESTAGESTATE data = {};
			data.Stage = stage;
			data.State = D3DDDITSS_DISABLETEXTURECOLORKEY;
			data.Value = TRUE;
			m_device.getOrigVtable().pfnSetTextureStageState(m_device, &data);
		}
		return result;
	}

	HRESULT DeviceState::pfnSetTextureStageState(const D3DDDIARG_TEXTURESTAGESTATE* data)
	{
		switch (data->State)
		{
		case D3DDDITSS_MINFILTER:
		case D3DDDITSS_MAGFILTER:
		case D3DDDITSS_MIPFILTER:
		case D3DDDITSS_MAXANISOTROPY:
			if (D3DTEXF_NONE != Config::textureFilter.getFilter())
			{
				return S_OK;
			}
			break;
		case D3DDDITSS_TEXTURECOLORKEYVAL:
			m_textureStageState[data->Stage][D3DDDITSS_DISABLETEXTURECOLORKEY] = FALSE;
			break;
		}
		return setStateArray(data, m_textureStageState[data->Stage], m_device.getOrigVtable().pfnSetTextureStageState);
	}

	HRESULT DeviceState::pfnSetVertexShaderConst(const D3DDDIARG_SETVERTEXSHADERCONST* data, const void* registers)
	{
		return setShaderConst(data, registers, m_vertexShaderConst, m_device.getOrigVtable().pfnSetVertexShaderConst);
	}

	HRESULT DeviceState::pfnSetVertexShaderConstB(const D3DDDIARG_SETVERTEXSHADERCONSTB* data, const BOOL* registers)
	{
		return setShaderConst(data, registers, m_vertexShaderConstB, m_device.getOrigVtable().pfnSetVertexShaderConstB);
	}

	HRESULT DeviceState::pfnSetVertexShaderConstI(const D3DDDIARG_SETVERTEXSHADERCONSTI* data, const INT* registers)
	{
		return setShaderConst(data, registers, m_vertexShaderConstI, m_device.getOrigVtable().pfnSetVertexShaderConstI);
	}

	HRESULT DeviceState::pfnSetVertexShaderDecl(HANDLE shader)
	{
		HRESULT result = setShader(shader, m_vertexShaderDecl, m_device.getOrigVtable().pfnSetVertexShaderDecl);
		if (SUCCEEDED(result))
		{
			auto it = m_vertexShaderDecls.find(shader);
			if (it != m_vertexShaderDecls.end())
			{
				m_device.getDrawPrimitive().setVertexShaderDecl(it->second);
			}
			else
			{
				m_device.getDrawPrimitive().setVertexShaderDecl({});
			}
		}
		return result;
	}

	HRESULT DeviceState::pfnSetVertexShaderFunc(HANDLE shader)
	{
		return setShader(shader, m_vertexShaderFunc, m_device.getOrigVtable().pfnSetVertexShaderFunc);
	}

	HRESULT DeviceState::pfnSetViewport(const D3DDDIARG_VIEWPORTINFO* data)
	{
		return setState(data, m_viewport, m_device.getOrigVtable().pfnSetViewport);
	}

	HRESULT DeviceState::pfnSetZRange(const D3DDDIARG_ZRANGE* data)
	{
		return setState(data, m_zRange, m_device.getOrigVtable().pfnSetZRange);
	}

	HRESULT DeviceState::pfnUpdateWInfo(const D3DDDIARG_WINFO* data)
	{
		D3DDDIARG_WINFO wInfo = *data;
		if (1.0f == wInfo.WNear && 1.0f == wInfo.WFar)
		{
			wInfo.WNear = 0.0f;
		}
		return setState(&wInfo, m_wInfo, m_device.getOrigVtable().pfnUpdateWInfo);
	}

	HRESULT DeviceState::deleteShader(HANDLE shader, HANDLE& currentShader,
		HRESULT(APIENTRY* origDeleteShaderFunc)(HANDLE, HANDLE))
	{
		if (shader == currentShader)
		{
			m_device.flushPrimitives();
		}

		HRESULT result = origDeleteShaderFunc(m_device, shader);
		if (SUCCEEDED(result) && shader == currentShader)
		{
			currentShader = nullptr;
		}
		return result;
	}

	void DeviceState::onDestroyResource(HANDLE resource)
	{
		for (UINT i = 0; i < m_textures.size(); ++i)
		{
			if (m_textures[i] == resource)
			{
				m_textures[i] = nullptr;
				m_device.getOrigVtable().pfnSetTexture(m_device, i, nullptr);
			}
		}

		if (m_renderTarget.hRenderTarget == resource)
		{
			m_renderTarget = {};
		}
		else if (m_depthStencil.hZBuffer == resource)
		{
			m_depthStencil = {};
		}
	}

	HRESULT DeviceState::setShader(HANDLE shader, HANDLE& currentShader,
		HRESULT(APIENTRY* origSetShaderFunc)(HANDLE, HANDLE))
	{
		if (shader == currentShader)
		{
			return S_OK;
		}

		m_device.flushPrimitives();
		HRESULT result = origSetShaderFunc(m_device, shader);
		if (SUCCEEDED(result))
		{
			currentShader = shader;
		}
		return result;
	}

	template <typename SetShaderConstData, typename ShaderConst, typename Registers>
	HRESULT DeviceState::setShaderConst(const SetShaderConstData* data, const Registers* registers,
		std::vector<ShaderConst>& shaderConst,
		HRESULT(APIENTRY* origSetShaderConstFunc)(HANDLE, const SetShaderConstData*, const Registers*))
	{
		if (data->Register + data->Count > shaderConst.size())
		{
			shaderConst.resize(data->Register + data->Count);
		}

		if (0 == memcmp(&shaderConst[data->Register], registers, data->Count * sizeof(ShaderConst)))
		{
			return S_OK;
		}

		m_device.flushPrimitives();
		HRESULT result = origSetShaderConstFunc(m_device, data, registers);
		if (SUCCEEDED(result))
		{
			memcpy(&shaderConst[data->Register], registers, data->Count * sizeof(ShaderConst));
		}
		return result;
	}

	template <typename StateData>
	HRESULT DeviceState::setState(const StateData* data, StateData& currentState,
		HRESULT(APIENTRY* origSetState)(HANDLE, const StateData*))
	{
		if (0 == memcmp(data, &currentState, sizeof(currentState)))
		{
			return S_OK;
		}

		m_device.flushPrimitives();
		HRESULT result = origSetState(m_device, data);
		if (SUCCEEDED(result))
		{
			currentState = *data;
		}
		return result;
	}

	template <typename StateData, UINT size>
	HRESULT DeviceState::setStateArray(const StateData* data, std::array<UINT, size>& currentState,
		HRESULT(APIENTRY* origSetState)(HANDLE, const StateData*))
	{
		if (data->State >= static_cast<INT>(currentState.size()))
		{
			m_device.flushPrimitives();
			return origSetState(m_device, data);
		}

		if (data->Value == currentState[data->State])
		{
			return S_OK;
		}

		m_device.flushPrimitives();
		HRESULT result = origSetState(m_device, data);
		if (SUCCEEDED(result))
		{
			currentState[data->State] = data->Value;
		}
		return result;
	}

	void DeviceState::updateConfig()
	{
		if (m_renderTarget.hRenderTarget)
		{
			auto renderTarget = m_renderTarget;
			m_renderTarget = {};
			pfnSetRenderTarget(&renderTarget);
		}

		if (m_depthStencil.hZBuffer)
		{
			auto depthStencil = m_depthStencil;
			m_depthStencil = {};
			pfnSetDepthStencil(&depthStencil);
		}

		for (UINT i = 0; i < m_textureStageState.size(); ++i)
		{
			updateTextureStageState(i, D3DDDITSS_MAGFILTER);
			updateTextureStageState(i, D3DDDITSS_MINFILTER);
			updateTextureStageState(i, D3DDDITSS_MIPFILTER);
			updateTextureStageState(i, D3DDDITSS_MAXANISOTROPY);
		}
	}

	void DeviceState::updateTextureStageState(UINT stage, D3DDDITEXTURESTAGESTATETYPE state)
	{
		D3DDDIARG_TEXTURESTAGESTATE data = {};
		data.Stage = stage;
		data.State = state;
		data.Value = mapTssValue(state, m_textureStageState[stage][state]);
		m_device.getOrigVtable().pfnSetTextureStageState(m_device, &data);
	}

	DeviceState::ScopedRenderState::ScopedRenderState(DeviceState& deviceState, const D3DDDIARG_RENDERSTATE& data)
		: m_deviceState(deviceState)
		, m_prevData{ data.State, deviceState.m_renderState[data.State] }
	{
		m_deviceState.m_device.getOrigVtable().pfnSetRenderState(m_deviceState.m_device, &data);
	}

	DeviceState::ScopedRenderState::~ScopedRenderState()
	{
		m_deviceState.m_device.getOrigVtable().pfnSetRenderState(m_deviceState.m_device, &m_prevData);
	}

	DeviceState::ScopedTexture::ScopedTexture(DeviceState& deviceState, UINT stage, HANDLE texture, UINT filter)
		: m_deviceState(deviceState)
		, m_stage(stage)
		, m_prevTexture(deviceState.m_textures[stage])
		, m_scopedAddressU(deviceState, { stage, D3DDDITSS_ADDRESSU, D3DTADDRESS_CLAMP })
		, m_scopedAddressV(deviceState, { stage, D3DDDITSS_ADDRESSV, D3DTADDRESS_CLAMP })
		, m_scopedMagFilter(deviceState, { stage, D3DDDITSS_MAGFILTER, filter })
		, m_scopedMinFilter(deviceState, { stage, D3DDDITSS_MINFILTER, filter })
		, m_scopedMipFilter(deviceState, { stage, D3DDDITSS_MIPFILTER, D3DTEXF_NONE })
		, m_scopedSrgbTexture(deviceState, { stage, D3DDDITSS_SRGBTEXTURE, D3DTEXF_LINEAR == filter })
		, m_scopedWrap(deviceState, { static_cast<D3DDDIRENDERSTATETYPE>(D3DDDIRS_WRAP0 + stage), 0 })
		, m_prevTextureColorKeyVal(deviceState.m_textureStageState[stage][D3DDDITSS_TEXTURECOLORKEYVAL])
		, m_prevDisableTextureColorKey(deviceState.m_textureStageState[stage][D3DDDITSS_DISABLETEXTURECOLORKEY])
	{
		m_deviceState.pfnSetTexture(stage, texture);

		D3DDDIARG_TEXTURESTAGESTATE data = {};
		data.Stage = stage;
		data.State = D3DDDITSS_DISABLETEXTURECOLORKEY;
		data.Value = TRUE;
		m_deviceState.m_device.getOrigVtable().pfnSetTextureStageState(m_deviceState.m_device, &data);
	}

	DeviceState::ScopedTexture::~ScopedTexture()
	{
		m_deviceState.pfnSetTexture(m_stage, m_prevTexture);

		D3DDDIARG_TEXTURESTAGESTATE data = {};
		data.Stage = m_stage;
		if (m_prevDisableTextureColorKey)
		{
			data.State = D3DDDITSS_DISABLETEXTURECOLORKEY;
			data.Value = TRUE;
		}
		else
		{
			data.State = D3DDDITSS_TEXTURECOLORKEYVAL;
			data.Value = m_prevTextureColorKeyVal;
		}
		m_deviceState.m_device.getOrigVtable().pfnSetTextureStageState(m_deviceState.m_device, &data);
	}

	DeviceState::ScopedTextureStageState::ScopedTextureStageState(
		DeviceState& deviceState, const D3DDDIARG_TEXTURESTAGESTATE& data)
		: m_deviceState(deviceState)
		, m_prevData{ data.Stage, data.State, mapTssValue(data.State, deviceState.m_textureStageState[data.Stage][data.State]) }
	{
		m_deviceState.m_device.getOrigVtable().pfnSetTextureStageState(m_deviceState.m_device, &data);
	}

	DeviceState::ScopedTextureStageState::~ScopedTextureStageState()
	{
		m_deviceState.m_device.getOrigVtable().pfnSetTextureStageState(m_deviceState.m_device, &m_prevData);
	}
}
