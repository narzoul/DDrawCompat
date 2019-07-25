#pragma once

#include <vector>

#include <d3d.h>
#include <d3dumddi.h>
#include <ddraw.h>

#include "D3dDdi/FormatInfo.h"

namespace DDraw
{
	class Surface;
}

namespace D3dDdi
{
	class Device;

	class Resource
	{
	public:
		static Resource create(Device& device, D3DDDIARG_CREATERESOURCE& data);
		static Resource create(Device& device, D3DDDIARG_CREATERESOURCE2& data);

		operator HANDLE() const { return m_handle; }

		HRESULT blt(D3DDDIARG_BLT data);
		HRESULT colorFill(const D3DDDIARG_COLORFILL& data);
		void destroy();
		HRESULT lock(D3DDDIARG_LOCK& data);
		void prepareForRendering(UINT subResourceIndex, bool isReadOnly);
		void setLockResource(Resource* lockResource);
		void setRootSurface(DDraw::Surface* rootSurface);
		HRESULT unlock(const D3DDDIARG_UNLOCK& data);

	private:
		class Data : public D3DDDIARG_CREATERESOURCE2
		{
		public:
			Data();
			Data(const D3DDDIARG_CREATERESOURCE& data);
			Data(const D3DDDIARG_CREATERESOURCE2& data);
			Data(const Data& other);
			Data& operator=(const Data& other);
			Data(Data&&) = default;
			Data& operator=(Data&&) = default;

			std::vector<D3DDDI_SURFACEINFO> surfaceData;
		};

		struct LockData
		{
			void* data;
			UINT pitch;
			UINT lockCount;
			bool isSysMemUpToDate;
			bool isVidMemUpToDate;
		};

		struct SysMemBltGuard
		{
			void* data;
			UINT pitch;

			SysMemBltGuard(Resource& resource, UINT subResourceIndex, bool isReadOnly);
		};

		Resource(Device& device, const D3DDDIARG_CREATERESOURCE& data);
		Resource(Device& device, const D3DDDIARG_CREATERESOURCE2& data);

		static HRESULT copySubResource(Resource& dstResource, Resource& srcResource, UINT subResourceIndex);

		template <typename Arg>
		static Resource create(Device& device, Arg& data, HRESULT(APIENTRY *createResourceFunc)(HANDLE, Arg*));

		HRESULT bltLock(D3DDDIARG_LOCK& data);
		HRESULT bltUnlock(const D3DDDIARG_UNLOCK& data);
		void copyToSysMem(UINT subResourceIndex);
		void copyToVidMem(UINT subResourceIndex);
		bool isOversized() const;
		void moveToSysMem(UINT subResourceIndex);
		void moveToVidMem(UINT subResourceIndex);
		void prepareSubResourceForRendering(UINT subResourceIndex, bool isReadOnly);
		void setSysMemUpToDate(UINT subResourceIndex, bool upToDate);
		void setVidMemUpToDate(UINT subResourceIndex, bool upToDate);
		HRESULT splitBlt(D3DDDIARG_BLT& data, UINT& subResourceIndex, RECT& rect, RECT& otherRect);

		template <typename Arg>
		HRESULT splitLock(Arg& data, HRESULT(APIENTRY *lockFunc)(HANDLE, Arg*));

		HRESULT sysMemPreferredBlt(const D3DDDIARG_BLT& data, Resource& srcResource);

		Device& m_device;
		HANDLE m_handle;
		Data m_origData;
		Data m_fixedData;
		FormatInfo m_formatInfo;
		DDraw::Surface* m_rootSurface;
		Resource* m_lockResource;
		std::vector<LockData> m_lockData;
	};
}
