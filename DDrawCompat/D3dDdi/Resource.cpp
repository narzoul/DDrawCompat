#include <type_traits>

#include <Common/HResultException.h>
#include <Common/Log.h>
#include <D3dDdi/Adapter.h>
#include <D3dDdi/Device.h>
#include <D3dDdi/KernelModeThunks.h>
#include <D3dDdi/Log/DeviceFuncsLog.h>
#include <D3dDdi/Resource.h>
#include <DDraw/Blitter.h>
#include <Gdi/VirtualScreen.h>

namespace
{
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
			0 != D3dDdi::getFormatInfo(data.Format).bytesPerPixel)
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

		const UINT bytesPerPixel = D3dDdi::getFormatInfo(data.Format).bytesPerPixel;

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
		, m_lockResource(nullptr)
		, m_canCreateLockResource(false)
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
				else if (m_fixedData.Flags.Primary)
				{
					return presentationBlt(data, it->second);
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
			ptr += data.Area.top * lockData.pitch + data.Area.left * m_formatInfo.bytesPerPixel;
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

	HRESULT Resource::colorFill(const D3DDDIARG_COLORFILL& data)
	{
		LOG_FUNC("Resource::colorFill", data);
		if (data.SubResourceIndex < m_lockData.size() && 0 != m_formatInfo.bytesPerPixel)
		{
			auto& lockData = m_lockData[data.SubResourceIndex];
			if (lockData.isSysMemUpToDate)
			{
				auto dstBuf = static_cast<BYTE*>(lockData.data) +
					data.DstRect.top * lockData.pitch + data.DstRect.left * m_formatInfo.bytesPerPixel;

				DDraw::Blitter::colorFill(dstBuf, lockData.pitch,
					data.DstRect.right - data.DstRect.left, data.DstRect.bottom - data.DstRect.top,
					m_formatInfo.bytesPerPixel, colorConvert(m_formatInfo, data.Color));

				if (lockData.isVidMemUpToDate)
				{
					setVidMemUpToDate(data.SubResourceIndex, false);
				}
				return LOG_RESULT(S_OK);
			}
		}
		prepareForRendering(data.SubResourceIndex, false);
		return LOG_RESULT(m_device.getOrigVtable().pfnColorFill(m_device, &data));
	}

	HRESULT Resource::copySubResource(HANDLE dstResource, HANDLE srcResource, UINT subResourceIndex)
	{
		LOG_FUNC("Resource::copySubResource", dstResource, srcResource, subResourceIndex);
		RECT rect = {};
		rect.right = m_fixedData.pSurfList[subResourceIndex].Width;
		rect.bottom = m_fixedData.pSurfList[subResourceIndex].Height;

		D3DDDIARG_BLT data = {};
		data.hSrcResource = srcResource;
		data.SrcSubResourceIndex = subResourceIndex;
		data.SrcRect = rect;
		data.hDstResource = dstResource;
		data.DstSubResourceIndex = subResourceIndex;
		data.DstRect = rect;

		HRESULT result = m_device.getOrigVtable().pfnBlt(m_device, &data);
		if (FAILED(result))
		{
			LOG_ONCE("ERROR: Resource::copySubResource failed: " << Compat::hex(result));
		}
		return LOG_RESULT(result);
	}

	void Resource::copyToSysMem(UINT subResourceIndex)
	{
		copySubResource(m_lockResource, m_handle, subResourceIndex);
		setSysMemUpToDate(subResourceIndex, true);
	}

	void Resource::createGdiLockResource()
	{
		auto gdiSurfaceDesc(Gdi::VirtualScreen::getSurfaceDesc(D3dDdi::KernelModeThunks::getMonitorRect()));
		if (!gdiSurfaceDesc.lpSurface)
		{
			return;
		}

		D3DDDI_SURFACEINFO surfaceInfo = {};
		surfaceInfo.Width = gdiSurfaceDesc.dwWidth;
		surfaceInfo.Height = gdiSurfaceDesc.dwHeight;
		surfaceInfo.pSysMem = gdiSurfaceDesc.lpSurface;
		surfaceInfo.SysMemPitch = gdiSurfaceDesc.lPitch;

		createSysMemResource({ surfaceInfo });
		if (m_lockResource)
		{
			copySubResource(m_handle, m_lockResource, 0);
			m_canCreateLockResource = false;
		}
	}

	void Resource::createLockResource()
	{
		std::vector<D3DDDI_SURFACEINFO> surfaceInfo(m_fixedData.SurfCount);
		m_lockBuffers.resize(m_fixedData.SurfCount);

		for (UINT i = 0; i < m_fixedData.SurfCount; ++i)
		{
			auto width = m_fixedData.pSurfList[i].Width;
			auto height = m_fixedData.pSurfList[i].Height;
			auto pitch = divCeil(width * m_formatInfo.bytesPerPixel, 8) * 8;
			m_lockBuffers[i].resize(pitch * height + 8);

			surfaceInfo[i].Width = width;
			surfaceInfo[i].Height = height;
			if (reinterpret_cast<std::uintptr_t>(m_lockBuffers[i].data()) % 16 == 0)
			{
				surfaceInfo[i].pSysMem = m_lockBuffers[i].data() + 8;
			}
			else
			{
				surfaceInfo[i].pSysMem = m_lockBuffers[i].data();
			}
			surfaceInfo[i].SysMemPitch = pitch;
		}

		createSysMemResource(surfaceInfo);
		if (!m_lockResource)
		{
			m_lockBuffers.clear();
		}
	}

	void Resource::createSysMemResource(const std::vector<D3DDDI_SURFACEINFO>& surfaceInfo)
	{
		LOG_FUNC("Resource::createSysMemResource", Compat::array(surfaceInfo.data(), surfaceInfo.size()));
		D3DDDIARG_CREATERESOURCE2 data = {};
		data.Format = m_fixedData.Format;
		data.Pool = D3DDDIPOOL_SYSTEMMEM;
		data.pSurfList = surfaceInfo.data();
		data.SurfCount = surfaceInfo.size();
		data.Rotation = D3DDDI_ROTATION_IDENTITY;
		data.Flags.Texture = m_fixedData.Flags.Texture;

		HRESULT result = S_OK;
		if (m_device.getOrigVtable().pfnCreateResource2)
		{
			result = m_device.getOrigVtable().pfnCreateResource2(m_device, &data);
		}
		else
		{
			result = m_device.getOrigVtable().pfnCreateResource(m_device,
				reinterpret_cast<D3DDDIARG_CREATERESOURCE*>(&data));
		}

		if (FAILED(result))
		{
			LOG_RESULT(nullptr);
			return;
		}

		m_lockResource = data.hResource;
		m_lockData.resize(surfaceInfo.size());
		for (std::size_t i = 0; i < surfaceInfo.size(); ++i)
		{
			m_lockData[i].data = const_cast<void*>(surfaceInfo[i].pSysMem);
			m_lockData[i].pitch = surfaceInfo[i].SysMemPitch;
			m_lockData[i].isSysMemUpToDate = false;
			m_lockData[i].isVidMemUpToDate = true;
		}
		LOG_RESULT(m_lockResource);
	}

	void Resource::copyToVidMem(UINT subResourceIndex)
	{
		copySubResource(m_handle, m_lockResource, subResourceIndex);
		setVidMemUpToDate(subResourceIndex, true);
	}

	template <typename Arg>
	Resource Resource::create(Device& device, Arg& data, HRESULT(APIENTRY *createResourceFunc)(HANDLE, Arg*))
	{
		if (D3DDDIFMT_VERTEXDATA == data.Format &&
			data.Flags.VertexBuffer &&
			data.Flags.MightDrawFromLocked &&
			D3DDDIPOOL_SYSTEMMEM != data.Pool)
		{
			const HRESULT D3DERR_NOTAVAILABLE = 0x8876086A;
			throw HResultException(D3DERR_NOTAVAILABLE);
		}

		Resource resource(device, data);
		Arg origData = data;
		fixResourceData(device, reinterpret_cast<D3DDDIARG_CREATERESOURCE&>(data));
		resource.m_fixedData = data;
		resource.m_formatInfo = getFormatInfo(data.Format);

		HRESULT result = createResourceFunc(device, &data);
		if (FAILED(result))
		{
			data = origData;
			throw HResultException(result);
		}

		resource.m_handle = data.hResource;
		resource.m_canCreateLockResource = D3DDDIPOOL_SYSTEMMEM != data.Pool &&
			0 != resource.m_formatInfo.bytesPerPixel &&
			!data.Flags.Primary;
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

	void Resource::destroyLockResource()
	{
		if (m_lockResource)
		{
			for (UINT i = 0; i < m_lockData.size(); ++i)
			{
				setSysMemUpToDate(i, false);
				setVidMemUpToDate(i, true);
			}
			m_device.getOrigVtable().pfnDestroyResource(m_device, m_lockResource);
			m_lockResource = nullptr;
			m_lockData.clear();
			m_lockBuffers.clear();
		}
	}

	void Resource::fixVertexData(UINT offset, UINT count, UINT stride)
	{
		if (!m_fixedData.Flags.MightDrawFromLocked ||
			!m_fixedData.pSurfList[0].pSysMem ||
			!(m_fixedData.Fvf & D3DFVF_XYZRHW))
		{
			return;
		}

		unsigned char* data = static_cast<unsigned char*>(const_cast<void*>(m_fixedData.pSurfList[0].pSysMem)) + offset;
		if (0.0f != reinterpret_cast<D3DTLVERTEX*>(data)->rhw)
		{
			return;
		}

		for (UINT i = 0; i < count; ++i)
		{
			if (0.0f == reinterpret_cast<D3DTLVERTEX*>(data)->rhw)
			{
				reinterpret_cast<D3DTLVERTEX*>(data)->rhw = 1.0f;
			}
			data += stride;
		}
	}

	void* Resource::getLockPtr(UINT subResourceIndex)
	{
		if (subResourceIndex < m_lockData.size())
		{
			return m_lockData[subResourceIndex].data;
		}
		else if (subResourceIndex < m_fixedData.SurfCount)
		{
			return const_cast<void*>(m_fixedData.pSurfList[subResourceIndex].pSysMem);
		}
		return nullptr;
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

		if (m_canCreateLockResource)
		{
			createLockResource();
			m_canCreateLockResource = false;
		}

		if (m_lockResource)
		{
			return bltLock(data);
		}

		return m_device.getOrigVtable().pfnLock(m_device, &data);
	}

	void Resource::moveToSysMem(UINT subResourceIndex)
	{
		copySubResource(m_lockResource, m_handle, subResourceIndex);
		setSysMemUpToDate(subResourceIndex, true);
		setVidMemUpToDate(subResourceIndex, false);
	}

	void Resource::moveToVidMem(UINT subResourceIndex)
	{
		copySubResource(m_handle, m_lockResource, subResourceIndex);
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

	HRESULT Resource::presentationBlt(const D3DDDIARG_BLT& data, Resource& srcResource)
	{
		if (data.SrcSubResourceIndex < srcResource.m_lockData.size())
		{
			if (srcResource.m_lockData[data.SrcSubResourceIndex].isVidMemUpToDate)
			{
				if (srcResource.m_lockData[data.SrcSubResourceIndex].isSysMemUpToDate)
				{
					copySubResource(srcResource.m_handle, srcResource.m_lockResource, data.SrcSubResourceIndex);
				}
			}
			else
			{
				srcResource.copyToVidMem(data.SrcSubResourceIndex);
			}
		}
		return m_device.getOrigVtable().pfnBlt(m_device, &data);
	}

	void Resource::setAsGdiResource(bool isGdiResource)
	{
		destroyLockResource();
		if (isGdiResource)
		{
			createGdiLockResource();
		}
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
						data.DstRect.top * dstGuard.pitch + data.DstRect.left * m_formatInfo.bytesPerPixel;
					auto srcBuf = static_cast<const BYTE*>(srcGuard.data) +
						data.SrcRect.top * srcGuard.pitch + data.SrcRect.left * m_formatInfo.bytesPerPixel;

					DDraw::Blitter::blt(
						dstBuf,
						dstGuard.pitch,
						data.DstRect.right - data.DstRect.left,
						data.DstRect.bottom - data.DstRect.top,
						srcBuf,
						srcGuard.pitch,
						(1 - 2 * data.Flags.MirrorLeftRight) * (data.SrcRect.right - data.SrcRect.left),
						(1 - 2 * data.Flags.MirrorUpDown) * (data.SrcRect.bottom - data.SrcRect.top),
						m_formatInfo.bytesPerPixel,
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
