#pragma once

#include <d3dtypes.h>
#include <d3dumddi.h>

#include <array>
#include <map>
#include <memory>
#include <vector>

#include <Common/BitSet.h>
#include <D3dDdi/ResourceDeleter.h>

const UINT D3DTEXF_NONE = 0;
const UINT D3DTEXF_POINT = 1;
const UINT D3DTEXF_LINEAR = 2;
const UINT D3DTEXF_ANISOTROPIC = 3;
const UINT D3DTEXF_SRGBREAD = 0x10000;
const UINT D3DTEXF_SRGBWRITE = 0x20000;
const UINT D3DTEXF_SRGB = D3DTEXF_SRGBREAD | D3DTEXF_SRGBWRITE;

namespace D3dDdi
{
	class Device;
	class Resource;

	class DeviceState
	{
	public:
		typedef std::array<BOOL, 1> ShaderConstB;
		typedef std::array<FLOAT, 4> ShaderConstF;
		typedef std::array<INT, 4> ShaderConstI;

		struct State
		{
			D3DDDIARG_SETDEPTHSTENCIL depthStencil;
			HANDLE pixelShader;
			std::array<UINT, D3DDDIRS_BLENDOPALPHA + 1> renderState;
			D3DDDIARG_SETRENDERTARGET renderTarget;
			RECT scissorRect;
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

		class TempPixelShaderConst
		{
		public:
			TempPixelShaderConst(DeviceState& state, const D3DDDIARG_SETPIXELSHADERCONST& data, const ShaderConstF* registers);
			~TempPixelShaderConst();

		private:
			DeviceState& m_state;
			D3DDDIARG_SETPIXELSHADERCONST m_data;
		};

		class TempPixelShaderConstB
		{
		public:
			TempPixelShaderConstB(DeviceState& state, const D3DDDIARG_SETPIXELSHADERCONSTB& data, const BOOL* registers);
			~TempPixelShaderConstB();

		private:
			DeviceState& m_state;
			D3DDDIARG_SETPIXELSHADERCONSTB m_data;
		};

		class TempPixelShaderConstI
		{
		public:
			TempPixelShaderConstI(DeviceState& state, const D3DDDIARG_SETPIXELSHADERCONSTI& data, const ShaderConstI* registers);
			~TempPixelShaderConstI();

		private:
			DeviceState& m_state;
			D3DDDIARG_SETPIXELSHADERCONSTI m_data;
		};

		class TempStateLock
		{
		public:
			TempStateLock(DeviceState& state)
				: m_state(state)
			{
				state.m_isLocked = true;
			}

			~TempStateLock()
			{
				m_state.m_isLocked = false;
			}

		private:
			DeviceState& m_state;
		};

		struct VertexDecl
		{
			std::vector<D3DDDIVERTEXELEMENT> elements;
			std::array<UINT, 8> texCoordOffset;
			std::array<UINT, 8> texCoordType;
			bool isTransformed;
		};

		DeviceState(Device& device);
		
		HRESULT pfnCreatePixelShader(D3DDDIARG_CREATEPIXELSHADER* data, const UINT* code);
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

		void setSpriteMode(bool spriteMode);
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

		void disableTextureClamp(UINT stage);
		void flush();
		const State& getAppState() const { return m_app; }
		const State& getCurrentState() const { return m_current; }
		Resource* getTextureResource(UINT stage);
		UINT getTextureStageCount() const;
		const VertexDecl& getVertexDecl() const;
		HANDLE getVertexFixupDecl() const { return m_vsVertexFixup.get(); }
		bool isLocked() const { return m_isLocked; }
		void onDestroyResource(Resource* resource, HANDLE resourceHandle);
		void updateConfig();
		void updateStreamSource();

	private:
		friend class ScopedDeviceState;

		enum ChangedState
		{
			CS_RENDER_STATE  = 1 << 0,
			CS_RENDER_TARGET = 1 << 1,
			CS_SHADER        = 1 << 2,
			CS_STREAM_SOURCE = 1 << 3,
			CS_TEXTURE_STAGE = 1 << 4
		};

		struct PixelShader
		{
			std::vector<UINT> tokens;
			std::unique_ptr<void, ResourceDeleter> modifiedPixelShader;
			UINT textureStageCount;
			bool isModified;
		};

		template <int N>
		std::unique_ptr<void, ResourceDeleter> createVertexShader(const BYTE(&code)[N])
		{
			return createVertexShader(code, N);
		}

		std::unique_ptr<void, ResourceDeleter> DeviceState::createVertexShader(const BYTE* code, UINT size);
		HRESULT deleteShader(HANDLE shader, HANDLE State::* shaderMember,
			HRESULT(APIENTRY* origDeleteShaderFunc)(HANDLE, HANDLE));

		bool isColorKeyUsed();
		HANDLE mapPixelShader(HANDLE shader);
		UINT mapRsValue(D3DDDIRENDERSTATETYPE state, UINT value);
		UINT mapTssValue(UINT stage, D3DDDITEXTURESTAGESTATETYPE state, UINT value);
		void prepareTextures();

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
		void setScissorRect(const RECT& rect);
		void setStreamSource(const D3DDDIARG_SETSTREAMSOURCE& streamSource);
		void setStreamSourceUm(const D3DDDIARG_SETSTREAMSOURCEUM& streamSourceUm, const void* umBuffer);
		bool setTexture(UINT stage, HANDLE texture);
		void setTextureStageState(const D3DDDIARG_TEXTURESTAGESTATE& tss);
		void setVertexShaderDecl(HANDLE decl);
		void setVertexShaderFunc(HANDLE shader);
		bool setViewport(const D3DDDIARG_VIEWPORTINFO& viewport);
		void setWInfo(const D3DDDIARG_WINFO& wInfo);
		bool setZRange(const D3DDDIARG_ZRANGE& zRange);

		void updateRenderStates();
		void updateRenderTarget();
		void updateShaders();
		void updateTextureColorKey(UINT stage);
		void updateTextureStages();
		void updateVertexFixupConstants(UINT width, UINT height, float sx, float sy);

		Device& m_device;
		State m_app;
		State m_current;
		std::array<ShaderConstF, 32> m_pixelShaderConst;
		std::array<ShaderConstB, 16> m_pixelShaderConstB;
		std::array<ShaderConstI, 16> m_pixelShaderConstI;
		std::array<ShaderConstF, 256> m_vertexShaderConst;
		std::array<ShaderConstB, 16> m_vertexShaderConstB;
		std::array<ShaderConstI, 16> m_vertexShaderConstI;
		std::map<HANDLE, VertexDecl> m_vertexShaderDecls;
		VertexDecl* m_vertexDecl;
		UINT m_changedStates;
		UINT m_maxChangedTextureStage;
		UINT m_usedTextureStages;
		BitSet<D3DDDIRS_ZENABLE, D3DDDIRS_BLENDOPALPHA> m_changedRenderStates;
		std::array<BitSet<D3DDDITSS_TEXTUREMAP, D3DDDITSS_TEXTURECOLORKEYVAL>, 8> m_changedTextureStageStates;
		std::unique_ptr<void, ResourceDeleter> m_vsVertexFixup;
		std::array<Resource*, 8> m_textureResource;
		std::map<HANDLE, PixelShader> m_pixelShaders;
		PixelShader* m_pixelShader;
		bool m_isLocked;
		bool m_spriteMode;
	};
}
