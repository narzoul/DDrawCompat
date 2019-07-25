#include <type_traits>

#include "Common/HResultException.h"
#include "Common/Log.h"
#include "D3dDdi/Adapter.h"
#include "D3dDdi/Device.h"
#include "D3dDdi/Log/DeviceFuncsLog.h"
#include "D3dDdi/Resource.h"
#include "DDraw/Blitter.h"
#include "DDraw/Surfaces/Surface.h"

namespace
{
	UINT getBytesPerPixel(D3DDDIFORMAT format);
	D3DDDI_RESOURCEFLAGS getResourceTypeFlags();
	void splitToTiles(D3DDDIARG_CREATERESOURCE& data, const UINT tileWidth, const UINT tileHeight);

	const UINT g_resourceTypeFlags = getResourceTypeFlags().Value;

	LONG divCeil(LONG n, LONG d)
	{
		return (n + d - 1) / d;
	}

	void fixResourceData(D3dDdi::Device& device, D3DDDIARG_CREATERESOURCE& data)
	{
		if (data.Flags.Primary)
		{
			data.Format = D3DDDIFMT_X8R8G8B8;
		}

		const bool isOffScreenPlain = 0 == (data.Flags.Value & g_resourceTypeFlags);
		if (D3DDDIPOOL_SYSTEMMEM == data.Pool &&
			(isOffScreenPlain || data.Flags.Texture) &&
			1 == data.SurfCount &&
			0 == data.pSurfList[0].Depth &&
			0 != getBytesPerPixel(data.Format))
		{
			const auto& caps = device.getAdapter().getD3dExtendedCaps();
			const auto& surfaceInfo = data.pSurfList[0];
			if (0 != caps.dwMaxTextureWidth && surfaceInfo.Width > caps.dwMaxTextureWidth ||
				0 != caps.dwMaxTextureHeight && surfaceInfo.Height > caps.dwMaxTextureHeight)
			{
				splitToTiles(data, caps.dwMaxTextureWidth, caps.dwMaxTextureHeight);
			}
		}
	}

	UINT getBytesPerPixel(D3DDDIFORMAT format)
	{
		switch (format)
		{
		case D3DDDIFMT_R3G3B2:
		case D3DDDIFMT_A8:
		case D3DDDIFMT_P8:
		case D3DDDIFMT_R8:
			return 1;

		case D3DDDIFMT_R5G6B5:
		case D3DDDIFMT_X1R5G5B5:
		case D3DDDIFMT_A1R5G5B5:
		case D3DDDIFMT_A4R4G4B4:
		case D3DDDIFMT_A8R3G3B2:
		case D3DDDIFMT_X4R4G4B4:
		case D3DDDIFMT_A8P8:
		case D3DDDIFMT_G8R8:
			return 2;

		case D3DDDIFMT_R8G8B8:
			return 3;

		case D3DDDIFMT_A8R8G8B8:
		case D3DDDIFMT_X8R8G8B8:
		case D3DDDIFMT_A8B8G8R8:
		case D3DDDIFMT_X8B8G8R8:
			return 4;

		default:
			return 0;
		}
	}

	D3DDDI_RESOURCEFLAGS getResourceTypeFlags()
	{
		D3DDDI_RESOURCEFLAGS flags = {};
		flags.RenderTarget = 1;
		flags.ZBuffer = 1;
		flags.DMap = 1;
		flags.Points = 1;
		flags.RtPatches = 1;
		flags.NPatches = 1;
		flags.Video = 1;
		flags.CaptureBuffer = 1;
		flags.Primary = 1;
		flags.Texture = 1;
		flags.CubeMap = 1;
		flags.VertexBuffer = 1;
		flags.IndexBuffer = 1;
		flags.DecodeRenderTarget = 1;
		flags.DecodeCompressedBuffer = 1;
		flags.VideoProcessRenderTarget = 1;
		flags.Overlay = 1;
		flags.TextApi = 1;
		return flags;
	}

	void splitToTiles(D3DDDIARG_CREATERESOURCE& data, const UINT tileWidth, const UINT tileHeight)
	{
		static std::vector<D3DDDI_SURFACEINFO> tiles;
		tiles.clear();

		const UINT bytesPerPixel = getBytesPerPixel(data.Format);

		for (UINT y = 0; y < data.pSurfList[0].Height; y += tileHeight)
		{
			for (UINT x = 0; x < data.pSurfList[0].Width; x += tileWidth)
			{
				D3DDDI_SURFACEINFO tile = {};
				tile.Width = min(data.pSurfList[0].Width - x, tileWidth);
				tile.Height = min(data.pSurfList[0].Height - y, tileHeight);
				tile.pSysMem = static_cast<const unsigned char*>(data.pSurfList[0].pSysMem) +
					y * data.pSurfList[0].SysMemPitch + x * bytesPerPixel;
				tile.SysMemPitch = data.pSurfList[0].SysMemPitch;
				tiles.push_back(tile);
			}
		}

		data.SurfCount = tiles.size();
		data.pSurfList = tiles.data();
		data.Flags.Texture = 0;
	}

	D3DDDIARG_CREATERESOURCE2 upgradeResourceData(const D3DDDIARG_CREATERESOURCE& data)
	{
		D3DDDIARG_CREATERESOURCE2 data2 = {};
		reinterpret_cast<D3DDDIARG_CREATERESOURCE&>(data2) = data;
		return data2;
	}
}

namespace D3dDdi
{
	Resource::Data::Data()
		: D3DDDIARG_CREATERESOURCE2{}
	{
	}

	Resource::Data::Data(const D3DDDIARG_CREATERESOURCE& data)
		: Data(upgradeResourceData(data))
	{
	}

	Resource::Data::Data(const D3DDDIARG_CREATERESOURCE2& data)
		: D3DDDIARG_CREATERESOURCE2(data)
	{
		surfaceData.reserve(data.SurfCount);
		for (UINT i = 0; i < data.SurfCount; ++i)
		{
			surfaceData.push_back(data.pSurfList[i]);
		}
		pSurfList = surfaceData.data();
	}

	Resource::Data::Data(const Data& other)
		: D3DDDIARG_CREATERESOURCE2(other)
		, surfaceData(other.surfaceData)
	{
		pSurfList = surfaceData.data();
	}

	Resource::Data& Resource::Data::operator=(const Data& other)
	{
		static_cast<D3DDDIARG_CREATERESOURCE2&>(*this) = other;
		surfaceData = other.surfaceData;
		pSurfList = surfaceData.data();
		return *this;
	}

	Resource::SysMemBltGuard::SysMemBltGuard(Resource& resource, UINT subResourceIndex, bool isReadOnly)
		: data(nullptr)
		, pitch(0)
	{
		if (D3DDDIPOOL_SYSTEMMEM == resource.m_fixedData.Pool)
		{
			data = const_cast<void*>(resource.m_fixedData.pSurfList[subResourceIndex].pSysMem);
			pitch = resource.m_fixedData.pSurfList[subResourceIndex].SysMemPitch;
		}
		else if (subResourceIndex < resource.m_lockData.size())
		{
			if (!resource.m_lockData[subResourceIndex].isSysMemUpToDate)
			{
				if (isReadOnly)
				{
					resource.copyToSysMem(subResourceIndex);
				}
				else
				{
					resource.moveToSysMem(subResourceIndex);
				}
			}
			else if (!isReadOnly && resource.m_lockData[subResourceIndex].isVidMemUpToDate)
			{
				resource.setVidMemUpToDate(subResourceIndex, false);
			}
			data = resource.m_lockData[subResourceIndex].data;
			pitch = resource.m_lockData[subResourceIndex].pitch;
		}
	}

	Resource::Resource(Device& device, const D3DDDIARG_CREATERESOURCE& data)
		: Resource(device, upgradeResourceData(data))
	{
	}

	Resource::Resource(Device& device, const D3DDDIARG_CREATERESOURCE2& data)
		: m_device(device)
		, m_handle(nullptr)
		, m_origData(data)
		, m_bytesPerPixel(0)
		, m_rootSurface(nullptr)
		, m_lockResource(nullptr)
	{
	}

	HRESULT Resource::blt(D3DDDIARG_BLT data)
	{
		if (isOversized())
		{
			m_device.prepareForRendering(data.hSrcResource, data.SrcSubResourceIndex, true);
			return splitBlt(data, data.DstSubResourceIndex, data.DstRect, data.SrcRect);
		}
		else
		{
			auto& resources = m_device.getResources();
			auto it = resources.find(data.hSrcResource);
			if (it != resources.end())
			{
				if (it->second.isOversized())
				{
					prepareForRendering(data.DstSubResourceIndex, false);
					return it->second.splitBlt(data, data.SrcSubResourceIndex, data.SrcRect, data.DstRect);
				}
				else
				{
					return sysMemPreferredBlt(data, it->second);
				}
			}
		}
		prepareForRendering(data.DstSubResourceIndex, false);
		return m_device.getOrigVtable().pfnBlt(m_device, &data);
	}

	HRESULT Resource::bltLock(D3DDDIARG_LOCK& data)
	{
		LOG_FUNC("Resource::bltLock", data);
		if (data.SubResourceIndex >= m_lockData.size())
		{
			return LOG_RESULT(m_device.getOrigVtable().pfnLock(m_device, &data));
		}

		auto& lockData = m_lockData[data.SubResourceIndex];

		if (!lockData.isSysMemUpToDate)
		{
			if (data.Flags.ReadOnly)
			{
				copyToSysMem(data.SubResourceIndex);
			}
			else
			{
				moveToSysMem(data.SubResourceIndex);
			}
		}
		else if (!data.Flags.ReadOnly && lockData.isVidMemUpToDate)
		{
			setVidMemUpToDate(data.SubResourceIndex, false);
		}

		unsigned char* ptr = static_cast<unsigned char*>(lockData.data);
		if (data.Flags.AreaValid)
		{
			ptr += data.Area.top * lockData.pitch + data.Area.left * m_bytesPerPixel;
		}

		data.pSurfData = ptr;
		data.Pitch = lockData.pitch;
		++lockData.lockCount;
		return LOG_RESULT(S_OK);
	}

	HRESULT Resource::bltUnlock(const D3DDDIARG_UNLOCK& data)
	{
		LOG_FUNC("Resource::bltUnlock", data);
		if (data.SubResourceIndex >= m_lockData.size())
		{
			return LOG_RESULT(m_device.getOrigVtable().pfnUnlock(m_device, &data));
		}

		if (0 != m_lockData[data.SubResourceIndex].lockCount)
		{
			--m_lockData[data.SubResourceIndex].lockCount;
		}
		return LOG_RESULT(S_OK);
	}

	HRESULT Resource::copySubResource(Resource& dstResource, Resource& srcResource, UINT subResourceIndex)
	{
		RECT rect = {};
		rect.right = dstResource.m_fixedData.pSurfList[subResourceIndex].Width;
		rect.bottom = dstResource.m_fixedData.pSurfList[subResourceIndex].Height;

		D3DDDIARG_BLT data = {};
		data.hSrcResource = srcResource;
		data.SrcSubResourceIndex = subResourceIndex;
		data.SrcRect = rect;
		data.hDstResource = dstResource;
		data.DstSubResourceIndex = subResourceIndex;
		data.DstRect = rect;

		HRESULT result = dstResource.m_device.getOrigVtable().pfnBlt(dstResource.m_device, &data);
		if (FAILED(result))
		{
			LOG_ONCE("ERROR: Resource::copySubResource failed: " << Compat::hex(result));
		}
		return result;
	}

	void Resource::copyToSysMem(UINT subResourceIndex)
	{
		copySubResource(*m_lockResource, *this, subResourceIndex);
		setSysMemUpToDate(subResourceIndex, true);
	}

	void Resource::copyToVidMem(UINT subResourceIndex)
	{
		copySubResource(*this, *m_lockResource, subResourceIndex);
		setVidMemUpToDate(subResourceIndex, true);
	}

	template <typename Arg>
	Resource Resource::create(Device& device, Arg& data, HRESULT(APIENTRY *createResourceFunc)(HANDLE, Arg*))
	{
		Resource resource(device, data);
		Arg origData = data;
		fixResourceData(device, reinterpret_cast<D3DDDIARG_CREATERESOURCE&>(data));
		resource.m_fixedData = data;
		resource.m_bytesPerPixel = getBytesPerPixel(data.Format);

		HRESULT result = createResourceFunc(device, &data);
		if (FAILED(result))
		{
			data = origData;
			throw HResultException(result);
		}

		resource.m_handle = data.hResource;
		data = origData;
		data.hResource = resource.m_handle;
		return resource;
	}

	Resource Resource::create(Device& device, D3DDDIARG_CREATERESOURCE& data)
	{
		return create(device, data, device.getOrigVtable().pfnCreateResource);
	}

	Resource Resource::create(Device& device, D3DDDIARG_CREATERESOURCE2& data)
	{
		return create(device, data, device.getOrigVtable().pfnCreateResource2);
	}

	void Resource::destroy()
	{
		if (m_rootSurface)
		{
			m_rootSurface->clearResources();
		}
	}

	bool Resource::isOversized() const
	{
		return m_fixedData.SurfCount != m_origData.SurfCount;
	}

	HRESULT Resource::lock(D3DDDIARG_LOCK& data)
	{
		if (isOversized())
		{
			if (0 != data.SubResourceIndex ||
				data.Flags.RangeValid || data.Flags.AreaValid || data.Flags.BoxValid)
			{
				LOG_ONCE("WARNING: Unsupported lock of oversized resource: " << data);
				return m_device.getOrigVtable().pfnLock(m_device, &data);
			}
			return splitLock(data, m_device.getOrigVtable().pfnLock);
		}
		else if (m_lockResource)
		{
			return bltLock(data);
		}

		return m_device.getOrigVtable().pfnLock(m_device, &data);
	}

	void Resource::moveToSysMem(UINT subResourceIndex)
	{
		copySubResource(*m_lockResource, *this, subResourceIndex);
		setSysMemUpToDate(subResourceIndex, true);
		setVidMemUpToDate(subResourceIndex, false);
	}

	void Resource::moveToVidMem(UINT subResourceIndex)
	{
		copySubResource(*this, *m_lockResource, subResourceIndex);
		setVidMemUpToDate(subResourceIndex, true);
		setSysMemUpToDate(subResourceIndex, false);
	}

	void Resource::prepareForRendering(UINT subResourceIndex, bool isReadOnly)
	{
		if (subResourceIndex < m_lockData.size())
		{
			prepareSubResourceForRendering(subResourceIndex, isReadOnly);
		}
		else if (UINT_MAX == subResourceIndex)
		{
			for (UINT i = 0; i < m_lockData.size(); ++i)
			{
				prepareSubResourceForRendering(i, isReadOnly);
			}
		}
	}

	void Resource::prepareSubResourceForRendering(UINT subResourceIndex, bool isReadOnly)
	{
		auto& lockData = m_lockData[subResourceIndex];
		if (0 == lockData.lockCount)
		{
			if (isReadOnly)
			{
				if (!lockData.isVidMemUpToDate)
				{
					copyToVidMem(subResourceIndex);
				}
			}
			else if (lockData.isVidMemUpToDate)
			{
				setSysMemUpToDate(subResourceIndex, false);
			}
			else
			{
				moveToVidMem(subResourceIndex);
			}
		}
	}

	void Resource::setLockResource(Resource* lockResource)
	{
		if (!m_lockResource == !lockResource)
		{
			return;
		}

		if (lockResource)
		{
			if (lockResource->m_fixedData.SurfCount != m_fixedData.SurfCount)
			{
				LOG_ONCE("ERROR: Lock surface count mismatch: " <<
					m_fixedData.surfaceData << " vs " << lockResource->m_fixedData.SurfCount);
				return;
			}
			m_lockData.resize(m_fixedData.SurfCount);
			for (UINT i = 0; i < m_fixedData.SurfCount; ++i)
			{
				m_lockData[i].data = const_cast<void*>(lockResource->m_fixedData.pSurfList[i].pSysMem);
				m_lockData[i].pitch = lockResource->m_fixedData.pSurfList[i].SysMemPitch;
				m_lockData[i].isSysMemUpToDate = true;
				m_lockData[i].isVidMemUpToDate = true;
			}

			m_lockResource = lockResource;
			if (m_fixedData.Flags.RenderTarget)
			{
				for (std::size_t i = 0; i < m_lockData.size(); ++i)
				{
					m_device.addDirtyRenderTarget(*this, i);
				}
			}
		}
		else
		{
			if (m_fixedData.Flags.RenderTarget)
			{
				for (std::size_t i = 0; i < m_lockData.size(); ++i)
				{
					if (m_lockData[i].isSysMemUpToDate)
					{
						m_device.removeDirtyRenderTarget(*this, i);
					}
				}
			}
			if (m_fixedData.Flags.Texture)
			{
				for (std::size_t i = 0; i < m_lockData.size(); ++i)
				{
					if (!m_lockData[i].isVidMemUpToDate)
					{
						m_device.removeDirtyTexture(*this, i);
					}
				}
			}
			m_lockResource = nullptr;
			m_lockData.clear();
		}
	}

	void Resource::setRootSurface(DDraw::Surface* rootSurface)
	{
		m_rootSurface = rootSurface;
	}

	void Resource::setSysMemUpToDate(UINT subResourceIndex, bool upToDate)
	{
		m_lockData[subResourceIndex].isSysMemUpToDate = upToDate;
		if (m_fixedData.Flags.RenderTarget)
		{
			if (upToDate)
			{
				m_device.addDirtyRenderTarget(*this, subResourceIndex);
			}
			else
			{
				m_device.removeDirtyRenderTarget(*this, subResourceIndex);
			}
		}
	}

	void Resource::setVidMemUpToDate(UINT subResourceIndex, bool upToDate)
	{
		m_lockData[subResourceIndex].isVidMemUpToDate = upToDate;
		if (m_fixedData.Flags.Texture)
		{
			if (upToDate)
			{
				m_device.removeDirtyTexture(*this, subResourceIndex);
			}
			else
			{
				m_device.addDirtyTexture(*this, subResourceIndex);
			}
		}
	}

	HRESULT Resource::splitBlt(D3DDDIARG_BLT& data, UINT& subResourceIndex, RECT& rect, RECT& otherRect)
	{
		LOG_FUNC("Resource::splitBlt", data, subResourceIndex, rect, otherRect);

		if (0 != subResourceIndex ||
			data.SrcRect.right - data.SrcRect.left != data.DstRect.right - data.DstRect.left ||
			data.SrcRect.bottom - data.SrcRect.top != data.DstRect.bottom - data.DstRect.top ||
			data.Flags.MirrorLeftRight ||
			data.Flags.MirrorUpDown ||
			data.Flags.Rotate)
		{
			LOG_ONCE("WARNING: Unsupported blt of oversized resource: " << data);
			return LOG_RESULT(m_device.getOrigVtable().pfnBlt(m_device, &data));
		}

		const auto& caps = m_device.getAdapter().getD3dExtendedCaps();
		const auto tilesPerRow = divCeil(m_origData.pSurfList[0].Width, caps.dwMaxTextureWidth);

		const RECT origRect = rect;
		const POINT otherRectOffset = { otherRect.left - rect.left, otherRect.top - rect.top };

		POINT tilePos = {};
		tilePos.y = rect.top / static_cast<LONG>(caps.dwMaxTextureHeight);

		RECT tileRect = {};
		tileRect.top = tilePos.y * caps.dwMaxTextureHeight;
		tileRect.bottom = tileRect.top + caps.dwMaxTextureHeight;

		while (tileRect.top < origRect.bottom)
		{
			tilePos.x = origRect.left / static_cast<LONG>(caps.dwMaxTextureWidth);
			tileRect.left = tilePos.x * caps.dwMaxTextureWidth;
			tileRect.right = tileRect.left + caps.dwMaxTextureWidth;

			while (tileRect.left < origRect.right)
			{
				IntersectRect(&rect, &tileRect, &origRect);
				otherRect = rect;
				OffsetRect(&otherRect, otherRectOffset.x, otherRectOffset.y);
				OffsetRect(&rect, -tileRect.left, -tileRect.top);
				subResourceIndex = tilePos.y * tilesPerRow + tilePos.x;

				HRESULT result = m_device.getOrigVtable().pfnBlt(m_device, &data);
				if (FAILED(result))
				{
					return LOG_RESULT(result);
				}

				++tilePos.x;
				tileRect.left += caps.dwMaxTextureWidth;
				tileRect.right += caps.dwMaxTextureWidth;
			}

			++tilePos.y;
			tileRect.top += caps.dwMaxTextureHeight;
			tileRect.bottom += caps.dwMaxTextureHeight;
		}

		return LOG_RESULT(S_OK);
	}

	HRESULT Resource::sysMemPreferredBlt(const D3DDDIARG_BLT& data, Resource& srcResource)
	{
		if (m_fixedData.Format == srcResource.m_fixedData.Format &&
			(D3DDDIPOOL_SYSTEMMEM == m_fixedData.Pool || data.Flags.MirrorLeftRight || data.Flags.MirrorUpDown ||
				(data.DstSubResourceIndex < m_lockData.size() && m_lockData[data.DstSubResourceIndex].isSysMemUpToDate)))
		{
			SysMemBltGuard srcGuard(srcResource, data.SrcSubResourceIndex, true);
			if (srcGuard.data)
			{
				SysMemBltGuard dstGuard(*this, data.DstSubResourceIndex, false);
				if (dstGuard.data)
				{
					auto dstBuf = static_cast<BYTE*>(dstGuard.data) +
						data.DstRect.top * dstGuard.pitch + data.DstRect.left * m_bytesPerPixel;
					auto srcBuf = static_cast<const BYTE*>(srcGuard.data) +
						data.SrcRect.top * srcGuard.pitch + data.SrcRect.left * m_bytesPerPixel;

					DDraw::Blitter::blt(
						dstBuf,
						dstGuard.pitch,
						data.DstRect.right - data.DstRect.left,
						data.DstRect.bottom - data.DstRect.top,
						srcBuf,
						srcGuard.pitch,
						(1 - 2 * data.Flags.MirrorLeftRight) * (data.SrcRect.right - data.SrcRect.left),
						(1 - 2 * data.Flags.MirrorUpDown) * (data.SrcRect.bottom - data.SrcRect.top),
						m_bytesPerPixel,
						data.Flags.DstColorKey ? reinterpret_cast<const DWORD*>(&data.ColorKey) : nullptr,
						data.Flags.SrcColorKey ? reinterpret_cast<const DWORD*>(&data.ColorKey) : nullptr);

					return S_OK;
				}
			}
		}

		prepareForRendering(data.DstSubResourceIndex, false);
		srcResource.prepareForRendering(data.SrcSubResourceIndex, true);
		return m_device.getOrigVtable().pfnBlt(m_device, &data);
	}

	template <typename Arg>
	HRESULT Resource::splitLock(Arg& data, HRESULT(APIENTRY *lockFunc)(HANDLE, Arg*))
	{
		LOG_FUNC("Resource::splitLock", data, lockFunc);
		std::remove_const<Arg>::type tmpData = data;
		HRESULT result = lockFunc(m_device, &data);
		if (SUCCEEDED(result))
		{
			for (UINT i = 1; i < m_fixedData.SurfCount; ++i)
			{
				tmpData.SubResourceIndex = i;
				lockFunc(m_device, &tmpData);
			}
		}
		return LOG_RESULT(result);
	}

	HRESULT Resource::unlock(const D3DDDIARG_UNLOCK& data)
	{
		if (isOversized())
		{
			if (0 != data.SubResourceIndex)
			{
				LOG_ONCE("WARNING: Unsupported unlock of oversized resource: " << data);
				return m_device.getOrigVtable().pfnUnlock(m_device, &data);
			}
			return splitLock(data, m_device.getOrigVtable().pfnUnlock);
		}
		else if (m_lockResource)
		{
			return bltUnlock(data);
		}

		return m_device.getOrigVtable().pfnUnlock(m_device, &data);
	}
}
