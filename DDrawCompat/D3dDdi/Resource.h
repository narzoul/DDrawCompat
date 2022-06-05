#pragma once

#include <memory>
#include <vector>

#include <d3d.h>
#include <d3dumddi.h>

#include <D3dDdi/FormatInfo.h>
#include <D3dDdi/ResourceDeleter.h>
#include <D3dDdi/SurfaceRepository.h>

namespace D3dDdi
{
	class Device;

	class Resource
	{
	public:
		Resource(Device& device, D3DDDIARG_CREATERESOURCE2& data);

		Resource(const Resource&) = delete;
		Resource(Resource&&) = delete;
		Resource& operator=(const Resource&) = delete;
		Resource& operator=(Resource&&) = delete;
		~Resource();

		operator HANDLE() const { return m_handle; }
		const Resource* getCustomResource() { return m_msaaSurface.resource ? m_msaaSurface.resource : m_msaaResolvedSurface.resource; }
		Device& getDevice() const { return m_device; }
		const D3DDDIARG_CREATERESOURCE2& getFixedDesc() const { return m_fixedData; }
		const D3DDDIARG_CREATERESOURCE2& getOrigDesc() const { return m_origData; }
		bool isClampable() const { return m_isClampable; }

		HRESULT blt(D3DDDIARG_BLT data);
		HRESULT colorFill(D3DDDIARG_COLORFILL data);
		void disableClamp();
		void* getLockPtr(UINT subResourceIndex);
		HRESULT lock(D3DDDIARG_LOCK& data);
		void onDestroyResource(HANDLE resource);
		Resource& prepareForBltSrc(const D3DDDIARG_BLT& data);
		Resource& prepareForBltDst(D3DDDIARG_BLT& data);
		Resource& prepareForBltDst(HANDLE& resource, UINT& subResourceIndex, RECT& rect);
		void prepareForCpuRead(UINT subResourceIndex);
		void prepareForCpuWrite(UINT subResourceIndex);
		Resource& prepareForGpuRead(UINT subResourceIndex);
		void prepareForGpuWrite(UINT subResourceIndex);
		HRESULT presentationBlt(D3DDDIARG_BLT data, Resource* srcResource);
		void scaleRect(RECT& rect);
		void setAsGdiResource(bool isGdiResource);
		void setAsPrimary();
		HRESULT unlock(const D3DDDIARG_UNLOCK& data);
		void updateConfig();

	private:
		class Data : public D3DDDIARG_CREATERESOURCE2
		{
		public:
			Data(const D3DDDIARG_CREATERESOURCE2& data);

			Data(const Data&) = delete;
			Data(Data&&) = delete;
			Data& operator=(const Data&) = delete;
			Data& operator=(Data&&) = delete;

			std::vector<D3DDDI_SURFACEINFO> surfaceData;
		};

		struct LockData
		{
			void* data;
			UINT pitch;
			UINT lockCount;
			long long qpcLastForcedLock;
			bool isSysMemUpToDate;
			bool isVidMemUpToDate;
			bool isMsaaUpToDate;
			bool isMsaaResolvedUpToDate;
			bool isRefLocked;
		};

		HRESULT bltLock(D3DDDIARG_LOCK& data);
		HRESULT bltViaCpu(D3DDDIARG_BLT data, Resource& srcResource);
		HRESULT bltViaGpu(D3DDDIARG_BLT data, Resource& srcResource);
		void clearRectExterior(UINT subResourceIndex, const RECT& rect);
		void clearRectInterior(UINT subResourceIndex, const RECT& rect);
		void clearUpToDateFlags(UINT subResourceIndex);
		void clipRect(UINT subResourceIndex, RECT& rect);
		HRESULT copySubResource(Resource& dstResource, Resource& srcResource, UINT subResourceIndex);
		HRESULT copySubResource(HANDLE dstResource, HANDLE srcResource, UINT subResourceIndex);
		HRESULT copySubResourceRegion(HANDLE dst, UINT dstIndex, const RECT& dstRect,
			HANDLE src, UINT srcIndex, const RECT& srcRect);
		void createGdiLockResource();
		void createLockResource();
		void createSysMemResource(const std::vector<D3DDDI_SURFACEINFO>& surfaceInfo);
		bool downscale(Resource*& rt, LONG& srcWidth, LONG& srcHeight, LONG dstWidth, LONG dstHeight);
		void fixResourceData();
		D3DDDIFORMAT getFormatConfig();
		std::pair<D3DDDIMULTISAMPLE_TYPE, UINT> getMultisampleConfig();
		const SurfaceRepository::Surface& getNextRenderTarget(Resource* currentRt, DWORD width, DWORD height);
		RECT getRect(UINT subResourceIndex);
		SIZE getScaledSize();
		bool isValidRect(UINT subResourceIndex, const RECT& rect);
		void loadFromLockRefResource(UINT subResourceIndex);
		void loadMsaaResource(UINT subResourceIndex);
		void loadMsaaResolvedResource(UINT subResourceIndex);
		void loadSysMemResource(UINT subResourceIndex);
		void loadVidMemResource(UINT subResourceIndex);
		void notifyLock(UINT subResourceIndex);
		void presentLayeredWindows(Resource& dst, UINT dstSubResourceIndex, const RECT& dstRect);
		HRESULT shaderBlt(D3DDDIARG_BLT& data, Resource& dstResource, Resource& srcResource);

		Device& m_device;
		HANDLE m_handle;
		Data m_origData;
		Data m_fixedData;
		FormatInfo m_formatInfo;
		std::unique_ptr<void, void(*)(void*)> m_lockBuffer;
		std::vector<LockData> m_lockData;
		std::unique_ptr<void, ResourceDeleter> m_lockResource;
		SurfaceRepository::Surface m_lockRefSurface;
		SurfaceRepository::Surface m_msaaSurface;
		SurfaceRepository::Surface m_msaaResolvedSurface;
		D3DDDIFORMAT m_formatConfig;
		std::pair<D3DDDIMULTISAMPLE_TYPE, UINT> m_multiSampleConfig;
		SIZE m_scaledSize;
		bool m_isOversized;
		bool m_isSurfaceRepoResource;
		bool m_isClampable;
		bool m_isPrimary;
	};
}
