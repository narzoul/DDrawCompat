#pragma once

#include <d3dtypes.h>
#include <d3dumddi.h>

#include <array>
#include <map>
#include <vector>

#include <Common/BitSet.h>

const UINT D3DTEXF_NONE = 0;
const UINT D3DTEXF_POINT = 1;
const UINT D3DTEXF_LINEAR = 2;
const UINT D3DTEXF_ANISOTROPIC = 3;

namespace D3dDdi
{
	class Device;

	class DeviceState
	{
	public:
		typedef std::array<BOOL, 1> ShaderConstB;
		typedef std::array<FLOAT, 4> ShaderConstF;
		typedef std::array<INT, 4> ShaderConstI;

		class TempPixelShaderConst
		{
		public:
			TempPixelShaderConst(DeviceState& state, const D3DDDIARG_SETPIXELSHADERCONST& data, const ShaderConstF* registers);
			~TempPixelShaderConst();

		private:
			DeviceState& m_state;
			D3DDDIARG_SETPIXELSHADERCONST m_data;
		};

		class TempStateLock
		{
		public:
			TempStateLock(DeviceState& state)
				: m_state(state)
				, m_prevChangedStates(state.m_changedStates)
			{
				state.m_changedStates = 0;
			}

			~TempStateLock()
			{
				m_state.m_changedStates = m_prevChangedStates;
			}

		private:
			DeviceState& m_state;
			UINT m_prevChangedStates;
		};

		DeviceState(Device& device);
		
		HRESULT pfnCreateVertexShaderDecl(D3DDDIARG_CREATEVERTEXSHADERDECL* data, const D3DDDIVERTEXELEMENT* vertexElements);
		HRESULT pfnDeletePixelShader(HANDLE shader);
		HRESULT pfnDeleteVertexShaderDecl(HANDLE shader);
		HRESULT pfnDeleteVertexShaderFunc(HANDLE shader);
		HRESULT pfnSetDepthStencil(const D3DDDIARG_SETDEPTHSTENCIL* data);
		HRESULT pfnSetPixelShader(HANDLE shader);
		HRESULT pfnSetPixelShaderConst(const D3DDDIARG_SETPIXELSHADERCONST* data, const FLOAT* registers);
		HRESULT pfnSetPixelShaderConstB(const D3DDDIARG_SETPIXELSHADERCONSTB* data, const BOOL* registers);
		HRESULT pfnSetPixelShaderConstI(const D3DDDIARG_SETPIXELSHADERCONSTI* data, const INT* registers);
		HRESULT pfnSetRenderState(const D3DDDIARG_RENDERSTATE* data);
		HRESULT pfnSetRenderTarget(const D3DDDIARG_SETRENDERTARGET* data);
		HRESULT pfnSetStreamSource(const D3DDDIARG_SETSTREAMSOURCE* data);
		HRESULT pfnSetStreamSourceUm(const D3DDDIARG_SETSTREAMSOURCEUM* data, const void* umBuffer);
		HRESULT pfnSetTexture(UINT stage, HANDLE texture);
		HRESULT pfnSetTextureStageState(const D3DDDIARG_TEXTURESTAGESTATE* data);
		HRESULT pfnSetVertexShaderConst(const D3DDDIARG_SETVERTEXSHADERCONST* data, const void* registers);
		HRESULT pfnSetVertexShaderConstB(const D3DDDIARG_SETVERTEXSHADERCONSTB* data, const BOOL* registers);
		HRESULT pfnSetVertexShaderConstI(const D3DDDIARG_SETVERTEXSHADERCONSTI* data, const INT* registers);
		HRESULT pfnSetVertexShaderDecl(HANDLE shader);
		HRESULT pfnSetVertexShaderFunc(HANDLE shader);
		HRESULT pfnSetViewport(const D3DDDIARG_VIEWPORTINFO* data);
		HRESULT pfnSetZRange(const D3DDDIARG_ZRANGE* data);
		HRESULT pfnUpdateWInfo(const D3DDDIARG_WINFO* data);

		void setTempDepthStencil(const D3DDDIARG_SETDEPTHSTENCIL& depthStencil);
		void setTempPixelShader(HANDLE shader);
		void setTempRenderState(const D3DDDIARG_RENDERSTATE& renderState);
		void setTempRenderTarget(const D3DDDIARG_SETRENDERTARGET& renderTarget);
		void setTempStreamSource(const D3DDDIARG_SETSTREAMSOURCE& streamSource);
		void setTempStreamSourceUm(const D3DDDIARG_SETSTREAMSOURCEUM& streamSourceUm, const void* umBuffer);
		void setTempTexture(UINT stage, HANDLE texture);
		void setTempTextureStageState(const D3DDDIARG_TEXTURESTAGESTATE& tss);
		void setTempVertexShaderDecl(HANDLE decl);
		void setTempViewport(const D3DDDIARG_VIEWPORTINFO& viewport);
		void setTempWInfo(const D3DDDIARG_WINFO& wInfo);
		void setTempZRange(const D3DDDIARG_ZRANGE& zRange);

		void flush();
		void onDestroyResource(HANDLE resource);
		void updateConfig();

	private:
		friend class ScopedDeviceState;

		enum ChangedState
		{
			CS_MISC          = 1 << 0,
			CS_RENDER_STATE  = 1 << 1,
			CS_RENDER_TARGET = 1 << 2,
			CS_SHADER        = 1 << 3,
			CS_STREAM_SOURCE = 1 << 4,
			CS_TEXTURE_STAGE = 1 << 5
		};

		struct State
		{
			D3DDDIARG_SETDEPTHSTENCIL depthStencil;
			HANDLE pixelShader;
			std::array<UINT, D3DDDIRS_BLENDOPALPHA + 1> renderState;
			D3DDDIARG_SETRENDERTARGET renderTarget;
			D3DDDIARG_SETSTREAMSOURCE streamSource;
			D3DDDIARG_SETSTREAMSOURCEUM streamSourceUm;
			const void* streamSourceUmBuffer;
			std::array<HANDLE, 8> textures;
			std::array<std::array<UINT, D3DDDITSS_TEXTURECOLORKEYVAL + 1>, 8> textureStageState;
			HANDLE vertexShaderDecl;
			HANDLE vertexShaderFunc;
			D3DDDIARG_VIEWPORTINFO viewport;
			D3DDDIARG_WINFO wInfo;
			D3DDDIARG_ZRANGE zRange;
		};

		HRESULT deleteShader(HANDLE shader, HANDLE State::* shaderMember,
			HRESULT(APIENTRY* origDeleteShaderFunc)(HANDLE, HANDLE));

		template <typename Data>
		void removeResource(HANDLE resource, Data State::* data, HANDLE Data::* resourceMember,
			HRESULT(DeviceState::* pfnSetResourceFunc)(const Data*));

		template <typename Data>
		bool setData(const Data& data, Data& currentData, HRESULT(APIENTRY* origSetData)(HANDLE, const Data*));

		bool setShader(HANDLE shader, HANDLE& currentShader, HRESULT(APIENTRY* origSetShaderFunc)(HANDLE, HANDLE));

		template <typename SetShaderConstData, typename ShaderConstArray, typename Register>
		HRESULT setShaderConst(const SetShaderConstData* data, const Register* registers,
			ShaderConstArray& shaderConstArray,
			HRESULT(APIENTRY* origSetShaderConstFunc)(HANDLE, const SetShaderConstData*, const Register*));

		void setDepthStencil(const D3DDDIARG_SETDEPTHSTENCIL& depthStencil);
		void setPixelShader(HANDLE shader);
		void setRenderState(const D3DDDIARG_RENDERSTATE& renderState);
		void setRenderTarget(const D3DDDIARG_SETRENDERTARGET& renderTarget);
		void setStreamSource(const D3DDDIARG_SETSTREAMSOURCE& streamSource);
		void setStreamSourceUm(const D3DDDIARG_SETSTREAMSOURCEUM& streamSourceUm, const void* umBuffer);
		bool setTexture(UINT stage, HANDLE texture);
		void setTextureStageState(const D3DDDIARG_TEXTURESTAGESTATE& tss);
		void setVertexShaderDecl(HANDLE decl);
		void setVertexShaderFunc(HANDLE shader);
		void setViewport(const D3DDDIARG_VIEWPORTINFO& viewport);
		void setWInfo(const D3DDDIARG_WINFO& wInfo);
		void setZRange(const D3DDDIARG_ZRANGE& zRange);

		void updateMisc();
		void updateRenderStates();
		void updateRenderTargets();
		void updateShaders();
		void updateStreamSource();
		void updateTextureColorKey(UINT stage);
		void updateTextureStages();

		Device& m_device;
		State m_app;
		State m_current;
		std::array<ShaderConstF, 32> m_pixelShaderConst;
		std::array<ShaderConstB, 16> m_pixelShaderConstB;
		std::array<ShaderConstI, 16> m_pixelShaderConstI;
		std::array<ShaderConstF, 256> m_vertexShaderConst;
		std::array<ShaderConstB, 16> m_vertexShaderConstB;
		std::array<ShaderConstI, 16> m_vertexShaderConstI;
		std::map<HANDLE, std::vector<D3DDDIVERTEXELEMENT>> m_vertexShaderDecls;
		UINT m_changedStates;
		UINT m_maxChangedTextureStage;
		BitSet<D3DDDIRS_ZENABLE, D3DDDIRS_BLENDOPALPHA> m_changedRenderStates;
		std::array<BitSet<D3DDDITSS_TEXTUREMAP, D3DDDITSS_TEXTURECOLORKEYVAL>, 8> m_changedTextureStageStates;
	};
}
