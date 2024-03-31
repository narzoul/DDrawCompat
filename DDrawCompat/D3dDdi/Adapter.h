#pragma once

#include <map>
#include <string>

#include <d3d.h>
#include <d3dnthal.h>
#include <d3dumddi.h>

#include <Common/CompatPtr.h>
#include <Common/Vector.h>
#include <DDraw/DirectDraw.h>
#include <Win32/DisplayMode.h>

namespace D3dDdi
{
	class Adapter
	{
	public:
		struct AdapterInfo
		{
			D3DNTHAL_D3DEXTENDEDCAPS d3dExtendedCaps;
			std::map<D3DDDIFORMAT, FORMATOP> formatOps;
			std::map<D3DDDIFORMAT, FORMATOP> fixedFormatOps;
			DWORD supportedZBufferBitDepths;
			bool isMsaaDepthResolveSupported;
			bool isD3D9On12;
		};

		Adapter(const D3DDDIARG_OPENADAPTER& data);
		Adapter(const Adapter&) = delete;
		Adapter(Adapter&&) = delete;
		Adapter& operator=(const Adapter&) = delete;
		Adapter& operator=(Adapter&&) = delete;

		operator HANDLE() const { return m_adapter; }

		SIZE getAspectRatio() const;
		const AdapterInfo& getInfo() const { return m_info; }
		LUID getLuid() const { return m_luid; }
		const auto& getMonitorInfo() const { return Win32::DisplayMode::getMonitorInfo(m_deviceName); }
		std::pair<D3DDDIMULTISAMPLE_TYPE, UINT> getMultisampleConfig(D3DDDIFORMAT format) const;
		const D3DDDI_ADAPTERFUNCS& getOrigVtable() const { return m_origVtable; }
		D3DDDIFORMAT getRenderColorDepthSrcFormat(D3DDDIFORMAT appFormat) const;
		D3DDDIFORMAT getRenderColorDepthDstFormat() const;
		CompatWeakPtr<IDirectDraw7> getRepository() const { return m_repository; }
		SIZE getScaledSize(Int2 size) const;
		bool isEmulatedRenderTargetFormat(D3DDDIFORMAT format) const;

		HRESULT pfnCloseAdapter();
		HRESULT pfnCreateDevice(D3DDDIARG_CREATEDEVICE* pCreateData);
		HRESULT pfnGetCaps(const D3DDDIARG_GETCAPS* pData);

		static void add(const D3DDDIARG_OPENADAPTER& data) { s_adapters.emplace(data.hAdapter, data); }
		static Adapter& get(HANDLE adapter) { return s_adapters.find(adapter)->second; }
		static void setRepository(LUID luid, CompatWeakPtr<IDirectDraw7> repository, bool isPrimary);

	private:
		const AdapterInfo& findInfo() const;

		template <typename Data>
		HRESULT getCaps(D3DDDICAPS_TYPE type, Data& data, UINT size = sizeof(Data)) const;

		SIZE getAspectRatio(SIZE appRes, SIZE displayRes) const;
		std::map<D3DDDIFORMAT, FORMATOP> getFixedFormatOps(const AdapterInfo& info) const;
		std::map<D3DDDIFORMAT, FORMATOP> getFormatOps() const;
		Float2 getScaleFactor() const;
		std::string getSupportedMsaaModes(const std::map<D3DDDIFORMAT, FORMATOP>& formatOps) const;
		DWORD getSupportedZBufferBitDepths(const std::map<D3DDDIFORMAT, FORMATOP>& formatOps) const;
		bool isEmulatedRenderTargetFormat(D3DDDIFORMAT format, const std::map<D3DDDIFORMAT, FORMATOP>& formatOps) const;
		bool isSupportedRttFormat(D3DDDIFORMAT format) const;

		HANDLE m_adapter;
		D3DDDI_ADAPTERFUNCS m_origVtable;
		UINT m_runtimeVersion;
		UINT m_driverVersion;
		LUID m_luid;
		std::wstring m_deviceName;
		CompatWeakPtr<IDirectDraw7> m_repository;
		const AdapterInfo& m_info;

		static std::map<HANDLE, Adapter> s_adapters;
		static std::map<LUID, AdapterInfo> s_adapterInfos;
	};
}
