#pragma once

#include <d3dtypes.h>
#include <d3dumddi.h>

#include <array>
#include <map>
#include <vector>

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
		typedef std::array<FLOAT, 4> ShaderConstF;
		typedef std::array<INT, 4> ShaderConstI;

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

		void onDestroyResource(HANDLE resource);

	private:
		HRESULT deleteShader(HANDLE shader, HANDLE& currentShader,
			HRESULT(APIENTRY* origDeleteShaderFunc)(HANDLE, HANDLE));
		HRESULT setShader(HANDLE shader, HANDLE& currentShader,
			HRESULT(APIENTRY* origSetShaderFunc)(HANDLE, HANDLE));

		template <typename SetShaderConstData, typename ShaderConst, typename Registers>
		HRESULT setShaderConst(const SetShaderConstData* data, const Registers* registers,
			std::vector<ShaderConst>& shaderConst,
			HRESULT(APIENTRY* origSetShaderConstFunc)(HANDLE, const SetShaderConstData*, const Registers*));

		template <typename StateData>
		HRESULT setState(const StateData* data, StateData& currentState,
			HRESULT(APIENTRY* origSetState)(HANDLE, const StateData*));

		template <typename StateData, UINT size>
		HRESULT setStateArray(const StateData* data, std::array<UINT, size>& currentState,
			HRESULT(APIENTRY* origSetState)(HANDLE, const StateData*));

		Device& m_device;
		D3DDDIARG_SETDEPTHSTENCIL m_depthStencil;
		HANDLE m_pixelShader;
		std::vector<ShaderConstF> m_pixelShaderConst;
		std::vector<BOOL> m_pixelShaderConstB;
		std::vector<ShaderConstI> m_pixelShaderConstI;
		std::array<UINT, D3DDDIRS_BLENDOPALPHA + 1> m_renderState;
		D3DDDIARG_SETRENDERTARGET m_renderTarget;
		D3DDDIARG_SETSTREAMSOURCE m_streamSource;
		D3DDDIARG_SETSTREAMSOURCEUM m_streamSourceUm;
		const void* m_streamSourceUmBuffer;
		std::array<HANDLE, 8> m_textures;
		std::array<std::array<UINT, D3DDDITSS_TEXTURECOLORKEYVAL + 1>, 8> m_textureStageState;
		std::vector<ShaderConstF> m_vertexShaderConst;
		std::vector<BOOL> m_vertexShaderConstB;
		std::vector<ShaderConstI> m_vertexShaderConstI;
		std::map<HANDLE, std::vector<D3DDDIVERTEXELEMENT>> m_vertexShaderDecls;
		HANDLE m_vertexShaderDecl;
		HANDLE m_vertexShaderFunc;
		D3DDDIARG_VIEWPORTINFO m_viewport;
		D3DDDIARG_WINFO m_wInfo;
		D3DDDIARG_ZRANGE m_zRange;

	public:
		template <auto setterMethod, auto dataMemberPtr>
		class ScopedData
		{
		public:
			typedef std::remove_reference_t<decltype(std::declval<DeviceState>().*dataMemberPtr)> Data;

			ScopedData(DeviceState& deviceState, const Data& data)
				: m_deviceState(deviceState)
				, m_prevData(deviceState.*dataMemberPtr)
			{
				(m_deviceState.*setterMethod)(&data);
			}

			~ScopedData()
			{
				(m_deviceState.*setterMethod)(&m_prevData);
			}

		protected:
			DeviceState& m_deviceState;
			Data m_prevData;
		};

		class ScopedDepthStencil : public ScopedData<&DeviceState::pfnSetDepthStencil, &DeviceState::m_depthStencil>
		{
		public:
			using ScopedData::ScopedData;
		};

		template <HRESULT(DeviceState::* setHandle)(HANDLE), HANDLE DeviceState::* storedHandle>
		class ScopedHandle
		{
		public:
			ScopedHandle(DeviceState& deviceState, HANDLE handle)
				: m_deviceState(deviceState)
				, m_prevHandle(deviceState.*storedHandle)
			{
				(m_deviceState.*setHandle)(handle);
			}

			~ScopedHandle()
			{
				if (m_prevHandle)
				{
					(m_deviceState.*setHandle)(m_prevHandle);
				}
			}

		private:
			DeviceState& m_deviceState;
			HANDLE m_prevHandle;
		};

		class ScopedPixelShader : public ScopedHandle<&DeviceState::pfnSetPixelShader, &DeviceState::m_pixelShader>
		{
		public:
			using ScopedHandle::ScopedHandle;
		};

		class ScopedPixelShaderConst
		{
		public:
			ScopedPixelShaderConst(
				DeviceState& deviceState, const D3DDDIARG_SETPIXELSHADERCONST& data, const ShaderConstF* registers)
				: m_deviceState(deviceState)
				, m_register(data.Register)
			{
				if (data.Register + data.Count > m_deviceState.m_pixelShaderConst.size())
				{
					m_deviceState.m_pixelShaderConst.resize(data.Register + data.Count);
				}

				auto it = deviceState.m_pixelShaderConst.begin() + data.Register;
				m_prevRegisters.assign(it, it + data.Count);
				m_deviceState.pfnSetPixelShaderConst(&data, reinterpret_cast<const FLOAT*>(registers));
			}

			~ScopedPixelShaderConst()
			{
				D3DDDIARG_SETPIXELSHADERCONST data = {};
				data.Register = m_register;
				data.Count = m_prevRegisters.size();
				m_deviceState.pfnSetPixelShaderConst(&data, reinterpret_cast<const FLOAT*>(m_prevRegisters.data()));
			}

		private:
			DeviceState& m_deviceState;
			UINT m_register;
			std::vector<ShaderConstF> m_prevRegisters;
		};

		class ScopedRenderState
		{
		public:
			ScopedRenderState(DeviceState& deviceState, const D3DDDIARG_RENDERSTATE& data)
				: m_deviceState(deviceState)
				, m_prevData{ data.State, deviceState.m_renderState[data.State] }
			{
				m_deviceState.pfnSetRenderState(&data);
			}

			~ScopedRenderState()
			{
				m_deviceState.pfnSetRenderState(&m_prevData);
			}

		private:
			DeviceState& m_deviceState;
			D3DDDIARG_RENDERSTATE m_prevData;
		};

		class ScopedRenderTarget : public ScopedData<&DeviceState::pfnSetRenderTarget, &DeviceState::m_renderTarget>
		{
		public:
			ScopedRenderTarget(DeviceState& deviceState, const D3DDDIARG_SETRENDERTARGET& data)
				: ScopedData(deviceState, data)
			{
				if (!m_prevData.hRenderTarget)
				{
					m_prevData = data;
				}
			}
		};

		class ScopedStreamSourceUm
		{
		public:
			ScopedStreamSourceUm(DeviceState& deviceState, const D3DDDIARG_SETSTREAMSOURCEUM& data, const void* umBuffer)
				: m_deviceState(deviceState)
				, m_prevStreamSource(deviceState.m_streamSource)
				, m_prevStreamSourceUm(deviceState.m_streamSourceUm)
				, m_prevStreamSourceUmBuffer(deviceState.m_streamSourceUmBuffer)
			{
				m_deviceState.pfnSetStreamSourceUm(&data, umBuffer);
			}

			~ScopedStreamSourceUm()
			{
				if (m_prevStreamSourceUmBuffer)
				{
					m_deviceState.pfnSetStreamSourceUm(&m_prevStreamSourceUm, m_prevStreamSourceUmBuffer);
				}
				else if (m_prevStreamSource.hVertexBuffer)
				{
					m_deviceState.pfnSetStreamSource(&m_prevStreamSource);
				}
			}

		private:
			DeviceState& m_deviceState;
			D3DDDIARG_SETSTREAMSOURCE m_prevStreamSource;
			D3DDDIARG_SETSTREAMSOURCEUM m_prevStreamSourceUm;
			const void* m_prevStreamSourceUmBuffer;
		};

		class ScopedTextureStageState
		{
		public:
			ScopedTextureStageState(DeviceState& deviceState, const D3DDDIARG_TEXTURESTAGESTATE& data);
			~ScopedTextureStageState();

		private:
			DeviceState& m_deviceState;
			D3DDDIARG_TEXTURESTAGESTATE m_prevData;
		};

		class ScopedTexture
		{
		public:
			ScopedTexture(DeviceState& deviceState, UINT stage, HANDLE texture, UINT filter);
			~ScopedTexture();

		private:
			DeviceState& m_deviceState;
			UINT m_stage;
			HANDLE m_prevTexture;
			ScopedTextureStageState m_scopedAddressU;
			ScopedTextureStageState m_scopedAddressV;
			ScopedTextureStageState m_scopedMagFilter;
			ScopedTextureStageState m_scopedMinFilter;
			ScopedTextureStageState m_scopedMipFilter;
			ScopedTextureStageState m_scopedSrgbTexture;
			ScopedRenderState m_scopedWrap;
			UINT m_prevTextureColorKeyVal;
			UINT m_prevDisableTextureColorKey;
		};

		class ScopedVertexShaderDecl : public ScopedHandle<&DeviceState::pfnSetVertexShaderDecl, &DeviceState::m_vertexShaderDecl>
		{
		public:
			using ScopedHandle::ScopedHandle;
		};

		class ScopedViewport : public ScopedData<&DeviceState::pfnSetViewport, &DeviceState::m_viewport>
		{
		public:
			using ScopedData::ScopedData;
		};

		class ScopedZRange : public ScopedData<&DeviceState::pfnSetZRange, &DeviceState::m_zRange>
		{
		public:
			using ScopedData::ScopedData;
		};
	};
}
