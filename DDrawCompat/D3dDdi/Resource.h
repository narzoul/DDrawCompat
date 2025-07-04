#pragma once

#include <memory>
#include <vector>

#include <d3d.h>
#include <d3dumddi.h>

#include <D3dDdi/FormatInfo.h>
#include <D3dDdi/ResourceDeleter.h>
#include <D3dDdi/SurfaceRepository.h>
#include <Gdi/Window.h>

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
		Resource* getCustomResource() const { return m_msaaSurface.resource ? m_msaaSurface.resource : m_msaaResolvedSurface.resource; }
		Device& getDevice() const { return m_device; }
		const D3DDDIARG_CREATERESOURCE2& getFixedDesc() const { return m_fixedData; }
		FORMATOP getFormatOp() const { return m_formatOp; }
		const D3DDDIARG_CREATERESOURCE2& getOrigDesc() const { return m_origData; }
		UINT getPaletteHandle() const { return m_paletteHandle; }
		bool isClampable() const { return m_isClampable; }
		void invalidatePalettizedTexture();

		HRESULT blt(D3DDDIARG_BLT data);
		HRESULT colorFill(D3DDDIARG_COLORFILL data);
		HRESULT copySubResourceRegion(UINT dstIndex, const RECT& dstRect,
			HANDLE src, UINT srcIndex, const RECT& srcRect);
		HRESULT depthFill(const D3DDDIARG_DEPTHFILL& data);
		void disableClamp();
		void* getLockPtr(UINT subResourceIndex);
		UINT getMappedColorKey(UINT colorKey) const;
		RECT getRect(UINT subResourceIndex) const;
		HRESULT lock(D3DDDIARG_LOCK& data);
		void onDestroyResource(HANDLE resource);
		Resource& prepareForBltSrc(const D3DDDIARG_BLT& data);
		Resource& prepareForBltDst(D3DDDIARG_BLT& data);
		Resource& prepareForBltDst(HANDLE& resource, UINT subResourceIndex, RECT& rect);
		void prepareForCpuRead(UINT subResourceIndex);
		void prepareForCpuWrite(UINT subResourceIndex);
		Resource& prepareForGpuRead(UINT subResourceIndex);
		void prepareForGpuReadAll();
		Resource& prepareForGpuWrite(UINT subResourceIndex);
		void prepareForGpuWriteAll();
		Resource& prepareForTextureRead(UINT stage);
		HRESULT presentationBlt(D3DDDIARG_BLT data, Resource* srcResource);
		void scaleRect(RECT& rect);
		void setAsGdiResource(bool isGdiResource);
		void setAsPrimary();
		void setPaletteHandle(UINT paletteHandle);
		HRESULT unlock(const D3DDDIARG_UNLOCK& data);
		void updateConfig();
		void updatePalettizedTexture();

		static void enableConfig(bool enable);
		static void setFormatOverride(D3DDDIFORMAT format);
		static void setReadOnlyLock(bool readOnly);

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
			long long qpcLastCpuAccess;
			bool isSysMemUpToDate;
			bool isVidMemUpToDate;
			bool isMsaaUpToDate;
			bool isMsaaResolvedUpToDate;
			bool isRefLocked;

			LockData() { memset(this, 0, sizeof(*this)); }
		};

		HRESULT bltLock(D3DDDIARG_LOCK& data);
		HRESULT bltViaCpu(D3DDDIARG_BLT data, Resource& srcResource);
		HRESULT bltViaGpu(D3DDDIARG_BLT data, Resource& srcResource);
		bool canCopySubResource(const D3DDDIARG_BLT& data, Resource& srcResource);
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
		void fixResourceData();
		D3DDDIFORMAT getFormatConfig();
		std::pair<D3DDDIMULTISAMPLE_TYPE, UINT> getMultisampleConfig();
		SIZE getScaledSize();
		bool isPalettizedTexture() const;
		bool isScaled(UINT subResourceIndex);
		bool isValidRect(UINT subResourceIndex, const RECT& rect);
		void loadFromLockRefResource(UINT subResourceIndex);
		void loadMsaaResource(UINT subResourceIndex);
		void loadMsaaResolvedResource(UINT subResourceIndex);
		void loadSysMemResource(UINT subResourceIndex);
		void loadVidMemResource(UINT subResourceIndex);
		void notifyLock(UINT subResourceIndex);
		void presentLayeredWindows(Resource& dst, UINT dstSubResourceIndex, const RECT& dstRect,
			std::vector<Gdi::Window::LayeredWindow> layeredWindows, const RECT& monitorRect);
		void resolveMsaaDepthBuffer();
		HRESULT shaderBlt(const D3DDDIARG_BLT& data, Resource& dstResource, Resource& srcResource, UINT filter);
		bool shouldBltViaCpu(const D3DDDIARG_BLT &data, Resource& srcResource);

		Device& m_device;
		HANDLE m_handle;
		Data m_origData;
		Data m_fixedData;
		FormatInfo m_formatInfo;
		FORMATOP m_formatOp;
		std::unique_ptr<void, void(*)(void*)> m_lockBuffer;
		std::vector<LockData> m_lockData;
		std::unique_ptr<void, ResourceDeleter> m_lockResource;
		SurfaceRepository::Surface m_lockRefSurface;
		SurfaceRepository::Surface m_msaaSurface;
		SurfaceRepository::Surface m_msaaResolvedSurface;
		SurfaceRepository::Surface m_nullSurface;
		SurfaceRepository::Surface m_paletteResolvedSurface;
		SurfaceRepository::Surface m_colorKeyedSurface;
		UINT m_colorKey;
		D3DDDIFORMAT m_formatConfig;
		std::pair<D3DDDIMULTISAMPLE_TYPE, UINT> m_multiSampleConfig;
		SIZE m_scaledSize;
		UINT m_paletteHandle;
		std::vector<bool> m_isPaletteResolvedSurfaceUpToDate;
		std::vector<bool> m_isColorKeyedSurfaceUpToDate;
		bool m_isOversized;
		bool m_isSurfaceRepoResource;
		bool m_isClampable;
		bool m_isPrimary;
		bool m_isPrimaryScalingNeeded;
	};
}
