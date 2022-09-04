#include <algorithm>

#include <Config/Config.h>
#include <Common/Log.h>
#include <D3dDdi/Device.h>
#include <D3dDdi/DeviceState.h>
#include <D3dDdi/DrawPrimitive.h>
#include <D3dDdi/Log/DeviceFuncsLog.h>
#include <D3dDdi/Resource.h>
#include <Shaders/VertexFixup.h>

#define LOG_DS LOG_DEBUG << "DeviceState::" << __func__ << ": "

namespace
{
	const HANDLE DELETED_RESOURCE = reinterpret_cast<HANDLE>(0xBAADBAAD);
}

namespace D3dDdi
{
	DeviceState::TempPixelShaderConst::TempPixelShaderConst(
		DeviceState& state, const D3DDDIARG_SETPIXELSHADERCONST& data, const ShaderConstF* registers)
		: m_state(state)
		, m_data(data)
	{
		state.m_device.getOrigVtable().pfnSetPixelShaderConst(state.m_device, &data, &registers[0][0]);
	}

	DeviceState::TempPixelShaderConst::~TempPixelShaderConst()
	{
		m_state.m_device.getOrigVtable().pfnSetPixelShaderConst(
			m_state.m_device, &m_data, &m_state.m_pixelShaderConst[m_data.Register][0]);
	}

	DeviceState::DeviceState(Device& device)
		: m_device(device)
		, m_app{}
		, m_current{}
		, m_pixelShaderConst{}
		, m_pixelShaderConstB{}
		, m_pixelShaderConstI{}
		, m_vertexShaderConst{}
		, m_vertexShaderConstB{}
		, m_vertexShaderConstI{}
		, m_changedStates(0)
		, m_maxChangedTextureStage(0)
		, m_changedTextureStageStates{}
		, m_vsVertexFixup(createVertexShader(g_vsVertexFixup))
		, m_textureResource{}
		, m_isLocked(false)
		, m_spriteMode(false)
	{
		const UINT D3DBLENDOP_ADD = 1;
		const UINT UNINITIALIZED_STATE = 0xBAADBAAD;

		m_device.getOrigVtable().pfnSetDepthStencil(m_device, &m_current.depthStencil);
		m_device.getOrigVtable().pfnSetPixelShader(m_device, nullptr);
		m_device.getOrigVtable().pfnSetRenderTarget(m_device, &m_current.renderTarget);
		m_device.getOrigVtable().pfnSetVertexShaderDecl(m_device, nullptr);
		m_device.getOrigVtable().pfnSetVertexShaderFunc(m_device, nullptr);
		m_device.getOrigVtable().pfnSetViewport(m_device, &m_current.viewport);
		m_device.getOrigVtable().pfnUpdateWInfo(m_device, &m_current.wInfo);
		m_device.getOrigVtable().pfnSetZRange(m_device, &m_current.zRange);

		m_current.renderState.fill(UNINITIALIZED_STATE);
		m_current.renderState[D3DDDIRS_ZENABLE] = D3DZB_TRUE;
		m_current.renderState[D3DDDIRS_FILLMODE] = D3DFILL_SOLID;
		m_current.renderState[D3DDDIRS_SHADEMODE] = D3DSHADE_GOURAUD;
		m_current.renderState[D3DDDIRS_LINEPATTERN] = 0;
		m_current.renderState[D3DDDIRS_ZWRITEENABLE] = TRUE;
		m_current.renderState[D3DDDIRS_ALPHATESTENABLE] = FALSE;
		m_current.renderState[D3DDDIRS_LASTPIXEL] = TRUE;
		m_current.renderState[D3DDDIRS_SRCBLEND] = D3DBLEND_ONE;
		m_current.renderState[D3DDDIRS_DESTBLEND] = D3DBLEND_ZERO;
		m_current.renderState[D3DDDIRS_CULLMODE] = D3DCULL_CCW;
		m_current.renderState[D3DDDIRS_ZFUNC] = D3DCMP_LESSEQUAL;
		m_current.renderState[D3DDDIRS_ALPHAREF] = 0;
		m_current.renderState[D3DDDIRS_ALPHAFUNC] = D3DCMP_ALWAYS;
		m_current.renderState[D3DDDIRS_DITHERENABLE] = FALSE;
		m_current.renderState[D3DDDIRS_ALPHABLENDENABLE] = FALSE;
		m_current.renderState[D3DDDIRS_FOGENABLE] = FALSE;
		m_current.renderState[D3DDDIRS_SPECULARENABLE] = FALSE;
		m_current.renderState[D3DDDIRS_ZVISIBLE] = 0;
		m_current.renderState[D3DDDIRS_FOGCOLOR] = 0;
		m_current.renderState[D3DDDIRS_FOGTABLEMODE] = D3DFOG_NONE;
		m_current.renderState[D3DDDIRS_FOGSTART] = 0;
		m_current.renderState[D3DDDIRS_FOGEND] = 0x3F800000;
		m_current.renderState[D3DDDIRS_FOGDENSITY] = 0x3F800000;
		m_current.renderState[D3DDDIRS_COLORKEYENABLE] = FALSE;
		m_current.renderState[D3DDDIRS_EDGEANTIALIAS] = 0;
		m_current.renderState[D3DDDIRS_ZBIAS] = 0;
		m_current.renderState[D3DDDIRS_RANGEFOGENABLE] = FALSE;
		for (UINT i = D3DDDIRS_STIPPLEPATTERN00; i <= D3DDDIRS_STIPPLEPATTERN31; ++i)
		{
			m_current.renderState[i] = 0;
		}
		m_current.renderState[D3DDDIRS_STENCILENABLE] = FALSE;
		m_current.renderState[D3DDDIRS_STENCILFAIL] = D3DSTENCILOP_KEEP;
		m_current.renderState[D3DDDIRS_STENCILZFAIL] = D3DSTENCILOP_KEEP;
		m_current.renderState[D3DDDIRS_STENCILPASS] = D3DSTENCILOP_KEEP;
		m_current.renderState[D3DDDIRS_STENCILFUNC] = D3DCMP_ALWAYS;
		m_current.renderState[D3DDDIRS_STENCILREF] = 0;
		m_current.renderState[D3DDDIRS_STENCILMASK] = 0xFFFFFFFF;
		m_current.renderState[D3DDDIRS_STENCILWRITEMASK] = 0xFFFFFFFF;
		m_current.renderState[D3DDDIRS_TEXTUREFACTOR] = 0xFFFFFFFF;
		for (UINT i = D3DDDIRS_WRAP0; i <= D3DDDIRS_WRAP7; ++i)
		{
			m_current.renderState[i] = 0;
		}
		m_current.renderState[D3DDDIRS_CLIPPING] = TRUE;
		m_current.renderState[D3DDDIRS_CLIPPLANEENABLE] = 0;
		m_current.renderState[D3DDDIRS_SOFTWAREVERTEXPROCESSING] = FALSE;
		m_current.renderState[D3DDDIRS_POINTSIZE_MAX] = 0x3F800000;
		m_current.renderState[D3DDDIRS_POINTSIZE] = 0x3F800000;
		m_current.renderState[D3DDDIRS_POINTSIZE_MIN] = 0;
		m_current.renderState[D3DDDIRS_POINTSPRITEENABLE] = 0;
		m_current.renderState[D3DDDIRS_MULTISAMPLEMASK] = 0xFFFFFFFF;
		m_current.renderState[D3DDDIRS_MULTISAMPLEANTIALIAS] = TRUE;
		m_current.renderState[D3DDDIRS_PATCHEDGESTYLE] = FALSE;
		m_current.renderState[D3DDDIRS_PATCHSEGMENTS] = 0x3F800000;
		m_current.renderState[D3DDDIRS_COLORWRITEENABLE] = 0xF;
		m_current.renderState[D3DDDIRS_BLENDOP] = D3DBLENDOP_ADD;
		m_current.renderState[D3DDDIRS_SRGBWRITEENABLE] = FALSE;

		for (UINT i = 0; i < m_current.renderState.size(); i++)
		{
			if (UNINITIALIZED_STATE != m_current.renderState[i])
			{
				D3DDDIARG_RENDERSTATE data = {};
				data.State = static_cast<D3DDDIRENDERSTATETYPE>(i);
				data.Value = m_current.renderState[i];
				m_device.getOrigVtable().pfnSetRenderState(m_device, &data);
			}
		}

		for (UINT i = 0; i < m_current.textures.size(); ++i)
		{
			m_device.getOrigVtable().pfnSetTexture(m_device, i, nullptr);
		}

		for (UINT i = 0; i < m_current.textureStageState.size(); ++i)
		{
			m_current.textureStageState[i].fill(UNINITIALIZED_STATE);
			m_current.textureStageState[i][D3DDDITSS_TEXCOORDINDEX] = i;

			// When ADDRESSU or ADDRESSV is set to CLAMP, their value is overridden by D3DTSS_ADDRESS.
			// Setting this to CLAMP makes them behave as expected, instead of as WRAP,
			// which would be the default init value set by the runtime.
			m_current.textureStageState[i][D3DTSS_ADDRESS] = D3DTADDRESS_CLAMP;

			m_current.textureStageState[i][D3DDDITSS_ADDRESSU] = D3DTADDRESS_WRAP;
			m_current.textureStageState[i][D3DDDITSS_ADDRESSV] = D3DTADDRESS_WRAP;
			m_current.textureStageState[i][D3DDDITSS_BORDERCOLOR] = 0;
			m_current.textureStageState[i][D3DDDITSS_MAGFILTER] = D3DTEXF_POINT;
			m_current.textureStageState[i][D3DDDITSS_MINFILTER] = D3DTEXF_POINT;
			m_current.textureStageState[i][D3DDDITSS_MIPFILTER] = D3DTEXF_NONE;
			m_current.textureStageState[i][D3DDDITSS_MIPMAPLODBIAS] = 0;
			m_current.textureStageState[i][D3DDDITSS_MAXMIPLEVEL] = 0;
			m_current.textureStageState[i][D3DDDITSS_MAXANISOTROPY] = 1;
			m_current.textureStageState[i][D3DDDITSS_TEXTURETRANSFORMFLAGS] = D3DTTFF_DISABLE;
			m_current.textureStageState[i][D3DDDITSS_SRGBTEXTURE] = FALSE;
			m_current.textureStageState[i][D3DDDITSS_ADDRESSW] = D3DTADDRESS_WRAP;
			m_current.textureStageState[i][D3DDDITSS_DISABLETEXTURECOLORKEY] = TRUE;

			for (UINT j = 0; j < m_current.textureStageState[i].size(); ++j)
			{
				if (UNINITIALIZED_STATE != m_current.textureStageState[i][j])
				{
					D3DDDIARG_TEXTURESTAGESTATE data = {};
					data.Stage = i;
					data.State = static_cast<D3DDDITEXTURESTAGESTATETYPE>(j);
					data.Value = m_current.textureStageState[i][j];
					m_device.getOrigVtable().pfnSetTextureStageState(m_device, &data);
				}
			}
		}

		m_app = m_current;
		updateConfig();
	}

	std::unique_ptr<void, ResourceDeleter> DeviceState::createVertexShader(const BYTE* code, UINT size)
	{
		D3DDDIARG_CREATEVERTEXSHADERFUNC data = {};
		data.Size = size;
		if (FAILED(m_device.getOrigVtable().pfnCreateVertexShaderFunc(m_device, &data, reinterpret_cast<const UINT*>(code))))
		{
			return nullptr;
		}
		return { data.ShaderHandle, ResourceDeleter(m_device, m_device.getOrigVtable().pfnDeleteVertexShaderFunc) };
	}

	HRESULT DeviceState::deleteShader(HANDLE shader, HANDLE State::* shaderMember,
		HRESULT(APIENTRY* origDeleteShaderFunc)(HANDLE, HANDLE))
	{
		if (shader == m_current.*shaderMember)
		{
			m_device.flushPrimitives();
		}

		HRESULT result = origDeleteShaderFunc(m_device, shader);
		if (SUCCEEDED(result))
		{
			if (shader == m_app.*shaderMember)
			{
				m_app.*shaderMember = nullptr;
			}
			if (shader == m_current.*shaderMember)
			{
				m_current.*shaderMember = nullptr;
			}
		}
		return result;
	}

	void DeviceState::disableTextureClamp(UINT stage)
	{
		auto resource = getTextureResource(stage);
		resource->disableClamp();
		m_changedStates |= CS_TEXTURE_STAGE;
		m_changedTextureStageStates[stage].set(D3DDDITSS_ADDRESSU);
		m_changedTextureStageStates[stage].set(D3DDDITSS_ADDRESSV);
		m_maxChangedTextureStage = max(stage, m_maxChangedTextureStage);
	}

	void DeviceState::flush()
	{
		if (0 == m_changedStates || m_isLocked)
		{
			return;
		}

		prepareTextures();

		if (m_changedStates & CS_RENDER_STATE)
		{
			updateRenderStates();
		}
		if (m_changedStates & CS_RENDER_TARGET)
		{
			updateRenderTarget();
		}
		if (m_changedStates & CS_SHADER)
		{
			updateShaders();
		}
		if (m_changedStates & CS_STREAM_SOURCE)
		{
			updateStreamSource();
		}
		if (m_changedStates & CS_TEXTURE_STAGE)
		{
			updateTextureStages();
		}

		m_changedStates = 0;
		m_maxChangedTextureStage = 0;
	}

	Resource* DeviceState::getTextureResource(UINT stage)
	{
		if (!m_app.textures[stage])
		{
			return nullptr;
		}
		if (!m_textureResource[stage] || *m_textureResource[stage] != m_app.textures[stage])
		{
			m_textureResource[stage] = m_device.getResource(m_app.textures[stage]);
		}
		return m_textureResource[stage];
	}

	const DeviceState::VertexDecl& DeviceState::getVertexDecl() const
	{
		static const VertexDecl emptyDecl = {};
		auto it = m_vertexShaderDecls.find(m_app.vertexShaderDecl);
		return it != m_vertexShaderDecls.end() ? it->second : emptyDecl;
	}

	UINT DeviceState::mapRsValue(D3DDDIRENDERSTATETYPE state, UINT value)
	{
		if (state >= D3DDDIRS_WRAP0 && state <= D3DDDIRS_WRAP7)
		{
			return value & (D3DWRAPCOORD_0 | D3DWRAPCOORD_1 | D3DWRAPCOORD_2 | D3DWRAPCOORD_3);
		}

		if (D3DDDIRS_COLORKEYENABLE == state && value)
		{
			UINT enable = FALSE;
			for (UINT i = 0; i < getVertexDecl().textureStageCount && !enable; ++i)
			{
				enable = !m_app.textureStageState[i][D3DDDITSS_DISABLETEXTURECOLORKEY];
			}
			return enable;
		}

		if (D3DDDIRS_MULTISAMPLEANTIALIAS == state)
		{
			return 0 != value && !m_spriteMode;
		}

		return value;
	}

	UINT DeviceState::mapTssValue(UINT stage, D3DDDITEXTURESTAGESTATETYPE state, UINT value)
	{
		switch (state)
		{
		case D3DDDITSS_ADDRESSU:
		case D3DDDITSS_ADDRESSV:
			if (m_spriteMode && D3DTADDRESS_CLAMP != value)
			{
				if (Config::Settings::SpriteTexCoord::CLAMP == Config::spriteTexCoord.get())
				{
					auto resource = getTextureResource(stage);
					if (resource && resource->isClampable())
					{
						return D3DTADDRESS_CLAMP;
					}
				}
				else if (Config::Settings::SpriteTexCoord::CLAMPALL == Config::spriteTexCoord.get())
				{
					return D3DTADDRESS_CLAMP;
				}
			}
			return value;

		case D3DDDITSS_MAGFILTER:
		case D3DDDITSS_MINFILTER:
		{
			auto filter = (m_spriteMode && 0 == stage) ? Config::spriteFilter.get() : Config::textureFilter.getFilter();
			return D3DTEXF_NONE == filter ? value : filter;
		}

		case D3DDDITSS_MIPFILTER:
			return D3DTEXF_NONE == Config::textureFilter.getMipFilter() ? value : Config::textureFilter.getMipFilter();

		case D3DDDITSS_MAXANISOTROPY:
			return D3DTEXF_NONE == Config::textureFilter.getFilter() ? value : Config::textureFilter.getMaxAnisotropy();
		}

		return value;
	}

	void DeviceState::onDestroyResource(Resource* resource, HANDLE resourceHandle)
	{
		for (UINT i = 0; i < m_current.textures.size(); ++i)
		{
			if (m_current.textures[i] == resourceHandle)
			{
				m_current.textures[i] = DELETED_RESOURCE;
			}
			if (m_app.textures[i] == resourceHandle)
			{
				pfnSetTexture(i, nullptr);
			}
			if (m_textureResource[i] == resource)
			{
				m_textureResource[i] = nullptr;
			}
		}

		removeResource(resourceHandle, &State::renderTarget, &D3DDDIARG_SETRENDERTARGET::hRenderTarget,
			&DeviceState::pfnSetRenderTarget);
		removeResource(resourceHandle, &State::depthStencil, &D3DDDIARG_SETDEPTHSTENCIL::hZBuffer,
			&DeviceState::pfnSetDepthStencil);
		removeResource(resourceHandle, &State::streamSource, &D3DDDIARG_SETSTREAMSOURCE::hVertexBuffer,
			&DeviceState::pfnSetStreamSource);
	}

	HRESULT DeviceState::pfnCreateVertexShaderDecl(
		D3DDDIARG_CREATEVERTEXSHADERDECL* data,
		const D3DDDIVERTEXELEMENT* vertexElements)
	{
		LOG_DEBUG << Compat::array(vertexElements, data->NumVertexElements);

		const UINT D3DDECLUSAGE_POSITION = 0;
		const UINT D3DDECLUSAGE_TEXCOORD = 5;
		const UINT D3DDECLUSAGE_POSITIONT = 9;

		std::vector<D3DDDIVERTEXELEMENT> ve(vertexElements, vertexElements + data->NumVertexElements);
		VertexDecl decl = {};
		decl.elements = ve;

		for (UINT i = 0; i < data->NumVertexElements; ++i)
		{
			if (D3DDECLUSAGE_TEXCOORD == vertexElements[i].Usage)
			{
				decl.texCoordOffset[vertexElements[i].UsageIndex] = vertexElements[i].Offset;
				decl.texCoordType[vertexElements[i].UsageIndex] = vertexElements[i].Type;
				if (vertexElements[i].UsageIndex >= decl.textureStageCount)
				{
					decl.textureStageCount = vertexElements[i].UsageIndex + 1;
				}
			}
			else if (D3DDECLUSAGE_POSITIONT == vertexElements[i].Usage)
			{
				ve[i].Usage = D3DDECLUSAGE_POSITION;
				decl.isTransformed = true;
			}
		}

		HRESULT result = m_device.getOrigVtable().pfnCreateVertexShaderDecl(m_device, data, ve.data());
		if (SUCCEEDED(result))
		{
			m_vertexShaderDecls[data->ShaderHandle] = decl;
		}
		return result;
	}

	HRESULT DeviceState::pfnDeletePixelShader(HANDLE shader)
	{
		return deleteShader(shader, &State::pixelShader, m_device.getOrigVtable().pfnDeletePixelShader);
	}

	HRESULT DeviceState::pfnDeleteVertexShaderDecl(HANDLE shader)
	{
		HRESULT result = deleteShader(shader, &State::vertexShaderDecl, m_device.getOrigVtable().pfnDeleteVertexShaderDecl);
		if (SUCCEEDED(result))
		{
			m_vertexShaderDecls.erase(shader);
		}
		return result;
	}

	HRESULT DeviceState::pfnDeleteVertexShaderFunc(HANDLE shader)
	{
		return deleteShader(shader, &State::vertexShaderFunc, m_device.getOrigVtable().pfnDeleteVertexShaderFunc);
	}

	HRESULT DeviceState::pfnSetDepthStencil(const D3DDDIARG_SETDEPTHSTENCIL* data)
	{
		m_app.depthStencil = *data;
		m_changedStates |= CS_RENDER_TARGET;
		m_device.setDepthStencil(data->hZBuffer);
		return S_OK;
	}

	HRESULT DeviceState::pfnSetPixelShader(HANDLE shader)
	{
		m_app.pixelShader = shader;
		m_changedStates |= CS_SHADER;
		return S_OK;
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
		m_app.renderState[data->State] = data->Value;
		m_changedRenderStates.set(data->State);
		m_changedStates |= CS_RENDER_STATE;
		return S_OK;
	}

	HRESULT DeviceState::pfnSetRenderTarget(const D3DDDIARG_SETRENDERTARGET* data)
	{
		m_app.renderTarget = *data;
		m_changedStates |= CS_RENDER_TARGET;
		m_device.setRenderTarget(*data);
		return S_OK;
	}

	HRESULT DeviceState::pfnSetStreamSource(const D3DDDIARG_SETSTREAMSOURCE* data)
	{
		m_app.streamSource = *data;
		m_app.streamSourceUm = {};
		m_app.streamSourceUmBuffer = nullptr;
		m_changedStates |= CS_STREAM_SOURCE;
		return S_OK;
	}

	HRESULT DeviceState::pfnSetStreamSourceUm(const D3DDDIARG_SETSTREAMSOURCEUM* data, const void* umBuffer)
	{
		m_app.streamSourceUm = *data;
		m_app.streamSourceUmBuffer = umBuffer;
		m_app.streamSource = {};
		m_changedStates |= CS_STREAM_SOURCE;
		return S_OK;
	}

	HRESULT DeviceState::pfnSetTexture(UINT stage, HANDLE texture)
	{
		m_app.textures[stage] = texture;
		m_changedStates |= CS_RENDER_STATE | CS_TEXTURE_STAGE;
		m_changedRenderStates.set(D3DDDIRS_COLORKEYENABLE);
		m_changedTextureStageStates[stage].set(D3DDDITSS_ADDRESSU);
		m_changedTextureStageStates[stage].set(D3DDDITSS_ADDRESSV);
		m_maxChangedTextureStage = max(stage, m_maxChangedTextureStage);
		return S_OK;
	}

	HRESULT DeviceState::pfnSetTextureStageState(const D3DDDIARG_TEXTURESTAGESTATE* data)
	{
		if (D3DTSS_ADDRESS == data->State)
		{
			m_app.textureStageState[data->Stage][D3DDDITSS_ADDRESSU] = data->Value;
			m_app.textureStageState[data->Stage][D3DDDITSS_ADDRESSV] = data->Value;
			m_changedTextureStageStates[data->Stage].set(D3DDDITSS_ADDRESSU);
			m_changedTextureStageStates[data->Stage].set(D3DDDITSS_ADDRESSV);
			m_maxChangedTextureStage = max(data->Stage, m_maxChangedTextureStage);
			return S_OK;
		}
		
		if (D3DDDITSS_TEXTURECOLORKEYVAL == data->State ||
			D3DDDITSS_DISABLETEXTURECOLORKEY == data->State)
		{
			if (D3DDDITSS_TEXTURECOLORKEYVAL == data->State)
			{
				m_app.textureStageState[data->Stage][D3DDDITSS_DISABLETEXTURECOLORKEY] = FALSE;
			}
			m_changedRenderStates.set(D3DDDIRS_COLORKEYENABLE);
			m_changedStates |= CS_RENDER_STATE;
		}

		m_app.textureStageState[data->Stage][data->State] = data->Value;
		m_changedTextureStageStates[data->Stage].set(data->State);
		m_maxChangedTextureStage = max(data->Stage, m_maxChangedTextureStage);
		return S_OK;
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
		m_app.vertexShaderDecl = shader;
		m_changedStates |= CS_SHADER;
		return S_OK;
	}

	HRESULT DeviceState::pfnSetVertexShaderFunc(HANDLE shader)
	{
		m_app.vertexShaderFunc = shader;
		m_changedStates |= CS_SHADER;
		return S_OK;
	}

	HRESULT DeviceState::pfnSetViewport(const D3DDDIARG_VIEWPORTINFO* data)
	{
		m_app.viewport = *data;
		m_changedStates |= CS_RENDER_TARGET;
		return S_OK;
	}

	HRESULT DeviceState::pfnSetZRange(const D3DDDIARG_ZRANGE* data)
	{
		m_app.zRange = *data;
		m_changedStates |= CS_RENDER_TARGET;
		return S_OK;
	}

	HRESULT DeviceState::pfnUpdateWInfo(const D3DDDIARG_WINFO* data)
	{
		m_app.wInfo = *data;
		m_changedStates |= CS_RENDER_TARGET;
		return S_OK;
	}

	void DeviceState::prepareTextures()
	{
		for (UINT stage = 0; stage < m_app.textures.size(); ++stage)
		{
			auto resource = getTextureResource(stage);
			if (resource)
			{
				resource->updatePalettizedTexture(stage);
			}
		}
	}

	template <typename Data>
	void DeviceState::removeResource(HANDLE resource, Data State::* data, HANDLE Data::* resourceMember,
		HRESULT(DeviceState::* pfnSetResourceFunc)(const Data*))
	{
		if (resource == m_current.*data.*resourceMember)
		{
			m_current.*data = {};
			m_current.*data.*resourceMember = DELETED_RESOURCE;
		}
		if (resource == m_app.*data.*resourceMember)
		{
			m_app.*data = {};
			(this->*pfnSetResourceFunc)(&(m_app.*data));
		}
	}

	template <typename Data>
	bool DeviceState::setData(const Data& data, Data& currentData, HRESULT(APIENTRY* origSetData)(HANDLE, const Data*))
	{
		if (0 == memcmp(&data, &currentData, sizeof(data)))
		{
			return false;
		}

		m_device.flushPrimitives();
		origSetData(m_device, &data);
		currentData = data;
		return true;
	}

	void DeviceState::setDepthStencil(const D3DDDIARG_SETDEPTHSTENCIL& depthStencil)
	{
		if (setData(depthStencil, m_current.depthStencil, m_device.getOrigVtable().pfnSetDepthStencil))
		{
			LOG_DS << depthStencil;
		}
	}

	void DeviceState::setPixelShader(HANDLE shader)
	{
		if (setShader(shader, m_current.pixelShader, m_device.getOrigVtable().pfnSetPixelShader))
		{
			LOG_DS << shader;
		}
	}

	void DeviceState::setRenderState(const D3DDDIARG_RENDERSTATE& renderState)
	{
		if (renderState.Value == m_current.renderState[renderState.State])
		{
			return;
		}

		m_device.flushPrimitives();
		m_device.getOrigVtable().pfnSetRenderState(m_device, &renderState);
		m_current.renderState[renderState.State] = renderState.Value;
		LOG_DS << renderState;
	}

	void DeviceState::setRenderTarget(const D3DDDIARG_SETRENDERTARGET& renderTarget)
	{
		m_device.flushPrimitives();
		m_device.getOrigVtable().pfnSetRenderTarget(m_device, &renderTarget);
		m_current.renderTarget = renderTarget;
		m_current.pixelShader = DELETED_RESOURCE;
		m_current.vertexShaderDecl = DELETED_RESOURCE;
		m_current.vertexShaderFunc = DELETED_RESOURCE;
		m_changedStates |= CS_SHADER;
		LOG_DS << renderTarget;
	}

	bool DeviceState::setShader(HANDLE shader, HANDLE& currentShader,
		HRESULT(APIENTRY* origSetShaderFunc)(HANDLE, HANDLE))
	{
		if (shader == currentShader)
		{
			return false;
		}

		m_device.flushPrimitives();
		origSetShaderFunc(m_device, shader);
		currentShader = shader;
		return true;
	}

	void DeviceState::setSpriteMode(bool spriteMode)
	{
		if (spriteMode != m_spriteMode)
		{
			m_spriteMode = spriteMode;
			m_changedStates |= CS_RENDER_STATE | CS_TEXTURE_STAGE;
			m_changedRenderStates.set(D3DDDIRS_MULTISAMPLEANTIALIAS);
			if (Config::Settings::SpriteTexCoord::ROUND == Config::spriteTexCoord.get())
			{
				m_changedTextureStageStates[0].set(D3DDDITSS_ADDRESSU);
				m_changedTextureStageStates[0].set(D3DDDITSS_ADDRESSV);
			}
			m_changedTextureStageStates[0].set(D3DDDITSS_MAGFILTER);
			m_changedTextureStageStates[0].set(D3DDDITSS_MINFILTER);

			D3DDDIARG_SETVERTEXSHADERCONSTB data = {};
			data.Register = 15;
			data.Count = 1;
			BOOL value = spriteMode && Config::Settings::SpriteTexCoord::ROUND == Config::spriteTexCoord.get();
			pfnSetVertexShaderConstB(&data, &value);
		}
	}

	void DeviceState::setStreamSource(const D3DDDIARG_SETSTREAMSOURCE& streamSource)
	{
		if (0 == memcmp(&streamSource, &m_current.streamSource, sizeof(streamSource)))
		{
			return;
		}

		m_device.getDrawPrimitive().setStreamSource(m_app.streamSource);
		m_current.streamSource = streamSource;
		m_current.streamSourceUm = {};
		m_current.streamSourceUmBuffer = nullptr;
		LOG_DS << streamSource;
	}

	void DeviceState::setStreamSourceUm(const D3DDDIARG_SETSTREAMSOURCEUM& streamSourceUm, const void* umBuffer)
	{
		if (umBuffer == m_current.streamSourceUmBuffer &&
			0 == memcmp(&streamSourceUm, &m_current.streamSourceUm, sizeof(streamSourceUm)))
		{
			return;
		}

		m_device.getDrawPrimitive().setStreamSourceUm(streamSourceUm, umBuffer);
		m_current.streamSource = {};
		m_current.streamSourceUm = streamSourceUm;
		m_current.streamSourceUmBuffer = umBuffer;
		LOG_DS << streamSourceUm << " " << umBuffer;
	}

	void DeviceState::setTempDepthStencil(const D3DDDIARG_SETDEPTHSTENCIL& depthStencil)
	{
		setDepthStencil(depthStencil);
		m_changedStates |= CS_RENDER_TARGET;
	}

	void DeviceState::setTempPixelShader(HANDLE shader)
	{
		setPixelShader(shader);
		m_changedStates |= CS_SHADER;
	}

	void DeviceState::setTempRenderState(const D3DDDIARG_RENDERSTATE& renderState)
	{
		setRenderState(renderState);
		m_changedStates |= CS_RENDER_STATE;
		m_changedRenderStates.set(renderState.State);
	}

	void DeviceState::setTempRenderTarget(const D3DDDIARG_SETRENDERTARGET& renderTarget)
	{
		setRenderTarget(renderTarget);
		m_changedStates |= CS_RENDER_TARGET;
	}

	void DeviceState::setTempStreamSource(const D3DDDIARG_SETSTREAMSOURCE& streamSource)
	{
		setStreamSource(streamSource);
		m_changedStates |= CS_STREAM_SOURCE;
	}

	void DeviceState::setTempStreamSourceUm(const D3DDDIARG_SETSTREAMSOURCEUM& streamSourceUm, const void* umBuffer)
	{
		setStreamSourceUm(streamSourceUm, umBuffer);
		m_changedStates |= CS_STREAM_SOURCE;
	}

	void DeviceState::setTempTexture(UINT stage, HANDLE texture)
	{
		setTexture(stage, texture);
		m_changedStates |= CS_TEXTURE_STAGE;
		m_maxChangedTextureStage = max(stage, m_maxChangedTextureStage);
	}

	void DeviceState::setTempTextureStageState(const D3DDDIARG_TEXTURESTAGESTATE& tss)
	{
		setTextureStageState(tss);
		m_changedStates |= CS_TEXTURE_STAGE;
		m_changedTextureStageStates[tss.Stage].set(tss.State);
		m_maxChangedTextureStage = max(tss.Stage, m_maxChangedTextureStage);
	}

	void DeviceState::setTempVertexShaderDecl(HANDLE decl)
	{
		setVertexShaderDecl(decl);
		m_changedStates |= CS_SHADER;
	}

	void DeviceState::setTempViewport(const D3DDDIARG_VIEWPORTINFO& viewport)
	{
		setViewport(viewport);
		m_changedStates |= CS_RENDER_TARGET;
	}

	void DeviceState::setTempWInfo(const D3DDDIARG_WINFO& wInfo)
	{
		setWInfo(wInfo);
		m_changedStates |= CS_RENDER_TARGET;
	}

	void DeviceState::setTempZRange(const D3DDDIARG_ZRANGE& zRange)
	{
		setZRange(zRange);
		m_changedStates |= CS_RENDER_TARGET;
	}

	bool DeviceState::setTexture(UINT stage, HANDLE texture)
	{
		if (texture == m_current.textures[stage])
		{
			return false;
		}

		m_device.flushPrimitives();
		m_device.getOrigVtable().pfnSetTexture(m_device, stage, texture);
		m_current.textures[stage] = texture;

		if (0 == stage && texture)
		{
			auto resource = (texture == m_app.textures[stage]) ? getTextureResource(stage) : m_device.getResource(texture);
			if (resource)
			{
				D3DDDIARG_SETVERTEXSHADERCONST data = {};
				data.Register = 253;
				data.Count = 1;

				auto& si = resource->getFixedDesc().pSurfList[0];
				ShaderConstF reg = { static_cast<float>(si.Width), static_cast<float>(si.Height),
					m_vertexShaderConst[253][2], m_vertexShaderConst[253][3] };

				if (0 != memcmp(&reg, &m_vertexShaderConst[data.Register], sizeof(reg)))
				{
					pfnSetVertexShaderConst(&data, &reg);
				}
			}
		}

		LOG_DS << stage << " " << texture;
		return true;
	}

	void DeviceState::setTextureStageState(const D3DDDIARG_TEXTURESTAGESTATE& tss)
	{
		if (tss.Value == m_current.textureStageState[tss.Stage][tss.State])
		{
			return;
		}

		m_device.flushPrimitives();
		m_device.getOrigVtable().pfnSetTextureStageState(m_device, &tss);
		m_current.textureStageState[tss.Stage][tss.State] = tss.Value;
		LOG_DS << tss;
	}

	void DeviceState::setVertexShaderDecl(HANDLE decl)
	{
		if (setShader(decl, m_current.vertexShaderDecl, m_device.getOrigVtable().pfnSetVertexShaderDecl))
		{
			LOG_DS << decl;
		}
	}

	void DeviceState::setVertexShaderFunc(HANDLE shader)
	{
		if (setShader(shader, m_current.vertexShaderFunc, m_device.getOrigVtable().pfnSetVertexShaderFunc))
		{
			LOG_DS << shader;
		}
	}

	bool DeviceState::setViewport(const D3DDDIARG_VIEWPORTINFO& viewport)
	{
		if (setData(viewport, m_current.viewport, m_device.getOrigVtable().pfnSetViewport))
		{
			LOG_DS << viewport;
			return true;
		}
		return false;
	}

	void DeviceState::setWInfo(const D3DDDIARG_WINFO& wInfo)
	{
		if (setData(wInfo, m_current.wInfo, m_device.getOrigVtable().pfnUpdateWInfo))
		{
			LOG_DS << wInfo;
		}
	}

	bool DeviceState::setZRange(const D3DDDIARG_ZRANGE& zRange)
	{
		if (setData(zRange, m_current.zRange, m_device.getOrigVtable().pfnSetZRange))
		{
			LOG_DS << zRange;
			return true;
		}
		return false;
	}

	template <typename SetShaderConstData, typename ShaderConstArray, typename Register>
	HRESULT DeviceState::setShaderConst(const SetShaderConstData* data, const Register* registers,
		ShaderConstArray& shaderConstArray,
		HRESULT(APIENTRY* origSetShaderConstFunc)(HANDLE, const SetShaderConstData*, const Register*))
	{
		m_device.flushPrimitives();
		HRESULT result = origSetShaderConstFunc(m_device, data, registers);
		if (SUCCEEDED(result))
		{
			memcpy(&shaderConstArray[data->Register], registers, data->Count * sizeof(ShaderConstArray::value_type));
		}
		return result;
	}

	void DeviceState::updateConfig()
	{
		m_changedStates |= CS_RENDER_STATE | CS_RENDER_TARGET | CS_TEXTURE_STAGE;
		m_changedRenderStates.set(D3DDDIRS_MULTISAMPLEANTIALIAS);
		for (UINT i = 0; i < m_changedTextureStageStates.size(); ++i)
		{
			m_changedTextureStageStates[i].set(D3DDDITSS_MINFILTER);
			m_changedTextureStageStates[i].set(D3DDDITSS_MAGFILTER);
			m_changedTextureStageStates[i].set(D3DDDITSS_MIPFILTER);
			m_changedTextureStageStates[i].set(D3DDDITSS_MAXANISOTROPY);
		}
		m_changedTextureStageStates[0].set(D3DDDITSS_ADDRESSU);
		m_changedTextureStageStates[0].set(D3DDDITSS_ADDRESSV);
		m_maxChangedTextureStage = m_changedTextureStageStates.size() - 1;
	}

	void DeviceState::updateRenderStates()
	{
		m_changedRenderStates.forEach([&](UINT stateIndex)
			{
				const auto state = static_cast<D3DDDIRENDERSTATETYPE>(stateIndex);
				setRenderState({ state, mapRsValue(state, m_app.renderState[state]) });
			});
		m_changedRenderStates.reset();
	}

	void DeviceState::updateRenderTarget()
	{
		auto vp = m_app.viewport;
		auto renderTarget = m_app.renderTarget;
		auto depthStencil = m_app.depthStencil;

		Resource* resource = m_device.getResource(renderTarget.hRenderTarget);
		if (resource && resource->getCustomResource())
		{
			resource->scaleRect(reinterpret_cast<RECT&>(vp));
			renderTarget.hRenderTarget = *resource->getCustomResource();
			
			resource = m_device.getResource(depthStencil.hZBuffer);
			if (resource && resource->getCustomResource())
			{
				depthStencil.hZBuffer = *resource->getCustomResource();
			}
		}

		setRenderTarget(renderTarget);
		setDepthStencil(depthStencil);
		setViewport(vp);

		auto wInfo = m_app.wInfo;
		if (1.0f == wInfo.WNear && 1.0f == wInfo.WFar)
		{
			wInfo.WNear = 0.0f;
		}
		setWInfo(wInfo);

		setZRange(m_app.zRange);

		updateVertexFixupConstants();
	}

	void DeviceState::updateShaders()
	{
		setPixelShader(m_app.pixelShader);
		setVertexShaderDecl(m_app.vertexShaderDecl);
		auto it = m_vertexShaderDecls.find(m_app.vertexShaderDecl);
		if (it != m_vertexShaderDecls.end() && it->second.isTransformed)
		{
			setVertexShaderFunc(m_vsVertexFixup.get());
		}
		else
		{
			setVertexShaderFunc(m_app.vertexShaderFunc);
		}
	}

	void DeviceState::updateStreamSource()
	{
		if (m_app.streamSource.hVertexBuffer)
		{
			setStreamSource(m_app.streamSource);
		}
		else if (m_app.streamSourceUmBuffer)
		{
			setStreamSourceUm(m_app.streamSourceUm, m_app.streamSourceUmBuffer);
		}
	}

	void DeviceState::updateTextureColorKey(UINT stage)
	{
		m_changedTextureStageStates[stage].reset(D3DDDITSS_DISABLETEXTURECOLORKEY);
		m_changedTextureStageStates[stage].reset(D3DDDITSS_TEXTURECOLORKEYVAL);
		if (!m_app.textures[stage])
		{
			return;
		}

		D3DDDIARG_TEXTURESTAGESTATE tss = {};
		tss.Stage = stage;

		if (m_app.textureStageState[stage][D3DDDITSS_DISABLETEXTURECOLORKEY])
		{
			tss.State = D3DDDITSS_DISABLETEXTURECOLORKEY;
			tss.Value = m_app.textureStageState[stage][D3DDDITSS_DISABLETEXTURECOLORKEY];
			m_current.textureStageState[stage][D3DDDITSS_DISABLETEXTURECOLORKEY] = tss.Value;
		}
		else
		{
			tss.State = D3DDDITSS_TEXTURECOLORKEYVAL;
			tss.Value = m_app.textureStageState[stage][D3DDDITSS_TEXTURECOLORKEYVAL];
			auto resource = getTextureResource(stage);
			if (resource && resource->getPalettizedTexture())
			{
				tss.Value = reinterpret_cast<DWORD&>(
					m_device.getPalette(resource->getPalettizedTexture()->getPaletteHandle())[tss.Value]);
			}
			m_current.textureStageState[stage][D3DDDITSS_TEXTURECOLORKEYVAL] = tss.Value;
			m_current.textureStageState[stage][D3DDDITSS_DISABLETEXTURECOLORKEY] = FALSE;
		}

		m_device.flushPrimitives();
		m_device.getOrigVtable().pfnSetTextureStageState(m_device, &tss);
		LOG_DS << tss;
	}

	void DeviceState::updateTextureStages()
	{
		for (UINT stage = 0; stage <= m_maxChangedTextureStage; ++stage)
		{
			if (setTexture(stage, m_app.textures[stage]) ||
				m_changedTextureStageStates[stage].test(D3DDDITSS_DISABLETEXTURECOLORKEY) ||
				m_changedTextureStageStates[stage].test(D3DDDITSS_TEXTURECOLORKEYVAL))
			{
				updateTextureColorKey(stage);
			}

			m_changedTextureStageStates[stage].forEach([&](UINT stateIndex)
				{
					const auto state = static_cast<D3DDDITEXTURESTAGESTATETYPE>(stateIndex);
					setTextureStageState({ stage, state, mapTssValue(stage, state, m_app.textureStageState[stage][state]) });
				});
			m_changedTextureStageStates[stage].reset();
		}
	}

	void DeviceState::updateVertexFixupConstants()
	{
		D3DDDIARG_SETVERTEXSHADERCONST data = {};
		data.Register = 253;
		data.Count = 3;

		const float stc = static_cast<float>(Config::spriteTexCoord.getParam()) / 100;
		const float apc = Config::alternatePixelCenter.get();
		const auto& vp = m_app.viewport;
		const auto& zr = m_current.zRange;
		const float sx = static_cast<float>(m_current.viewport.Width) / m_app.viewport.Width;
		const float sy = static_cast<float>(m_current.viewport.Height) / m_app.viewport.Height;

		ShaderConstF registers[3] = {
			{ m_vertexShaderConst[253][0], m_vertexShaderConst[253][1], stc, stc },
			{ 0.5f + apc - 0.5f / sx - vp.X - vp.Width / 2, 0.5f + apc - 0.5f / sy - vp.Y - vp.Height / 2, -zr.MinZ, 0.0f },
			{ 2.0f / vp.Width, -2.0f / vp.Height, 1.0f / (zr.MaxZ - zr.MinZ), 1.0f }
		};

		if (0 != memcmp(registers, &m_vertexShaderConst[data.Register], sizeof(registers)))
		{
			pfnSetVertexShaderConst(&data, registers);
		}
	}
}
