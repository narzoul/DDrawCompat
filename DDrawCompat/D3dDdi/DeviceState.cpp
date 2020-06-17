#include <D3dDdi/Device.h>
#include <D3dDdi/DeviceState.h>

namespace
{
	bool operator==(const D3DDDIARG_ZRANGE& lhs, const D3DDDIARG_ZRANGE& rhs)
	{
		return lhs.MinZ == rhs.MinZ && lhs.MaxZ == rhs.MaxZ;
	}

	bool operator==(const D3DDDIARG_WINFO& lhs, const D3DDDIARG_WINFO& rhs)
	{
		return lhs.WNear == rhs.WNear && lhs.WFar == rhs.WFar;
	}
}

namespace D3dDdi
{
	DeviceState::DeviceState(Device& device)
		: m_device(device)
		, m_pixelShader(nullptr)
		, m_textures{}
		, m_vertexShaderDecl(nullptr)
		, m_vertexShaderFunc(nullptr)
		, m_wInfo{ NAN, NAN }
		, m_zRange{ NAN, NAN }
	{
		m_renderState.fill(0xBAADBAAD);
		for (UINT i = 0; i < m_textureStageState.size(); ++i)
		{
			m_textureStageState[i].fill(0xBAADBAAD);
		}
	}

	HRESULT DeviceState::pfnDeletePixelShader(HANDLE shader)
	{
		return deleteShader(shader, m_pixelShader, m_device.getOrigVtable().pfnDeletePixelShader);
	}

	HRESULT DeviceState::pfnDeleteVertexShaderDecl(HANDLE shader)
	{
		return deleteShader(shader, m_vertexShaderDecl, m_device.getOrigVtable().pfnDeleteVertexShaderDecl);
	}

	HRESULT DeviceState::pfnDeleteVertexShaderFunc(HANDLE shader)
	{
		return deleteShader(shader, m_vertexShaderFunc, m_device.getOrigVtable().pfnDeleteVertexShaderFunc);
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
		return setStateArray(data, m_renderState, m_device.getOrigVtable().pfnSetRenderState);
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
		}
		return result;
	}

	HRESULT DeviceState::pfnSetTextureStageState(const D3DDDIARG_TEXTURESTAGESTATE* data)
	{
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
		return setShader(shader, m_vertexShaderDecl, m_device.getOrigVtable().pfnSetVertexShaderDecl);
	}

	HRESULT DeviceState::pfnSetVertexShaderFunc(HANDLE shader)
	{
		return setShader(shader, m_vertexShaderFunc, m_device.getOrigVtable().pfnSetVertexShaderFunc);
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
		if (*data == currentState)
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
}
