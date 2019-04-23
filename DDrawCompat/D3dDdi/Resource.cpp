#include <type_traits>

#include "Common/HResultException.h"
#include "Common/Log.h"
#include "D3dDdi/Adapter.h"
#include "D3dDdi/Device.h"
#include "D3dDdi/Log/DeviceFuncsLog.h"
#include "D3dDdi/Resource.h"

namespace
{
	D3DDDI_RESOURCEFLAGS getResourceTypeFlags();
	bool isVidMemPool(D3DDDI_POOL pool);
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
			0 != D3dDdi::getBytesPerPixel(data.Format))
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

	bool isVidMemPool(D3DDDI_POOL pool)
	{
		return D3DDDIPOOL_VIDEOMEMORY == pool ||
			D3DDDIPOOL_LOCALVIDMEM == pool ||
			D3DDDIPOOL_NONLOCALVIDMEM == pool;
	}

	void splitToTiles(D3DDDIARG_CREATERESOURCE& data, const UINT tileWidth, const UINT tileHeight)
	{
		static std::vector<D3DDDI_SURFACEINFO> tiles;
		tiles.clear();

		const UINT bytesPerPixel = D3dDdi::getBytesPerPixel(data.Format);

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

	Resource::Resource(Device& device, const D3DDDIARG_CREATERESOURCE& data)
		: Resource(device, upgradeResourceData(data))
	{
	}

	Resource::Resource(Device& device, const D3DDDIARG_CREATERESOURCE2& data)
		: m_device(device)
		, m_handle(nullptr)
		, m_origData(data)
	{
	}

	HRESULT Resource::blt(D3DDDIARG_BLT data)
	{
		if (isOversized())
		{
			return splitBlt(data, data.DstSubResourceIndex, data.DstRect, data.SrcRect);
		}
		else
		{
			auto& resources = m_device.getResources();
			auto it = resources.find(data.hSrcResource);
			if (it != resources.end() && it->second.isOversized())
			{
				return it->second.splitBlt(data, data.SrcSubResourceIndex, data.SrcRect, data.DstRect);
			}
		}
		return m_device.getOrigVtable().pfnBlt(m_device, &data);
	}

	template <typename Arg>
	Resource Resource::create(Device& device, Arg& data, HRESULT(APIENTRY *createResourceFunc)(HANDLE, Arg*))
	{
		Resource resource(device, data);
		Arg origData = data;
		auto& baseData = reinterpret_cast<D3DDDIARG_CREATERESOURCE&>(data);
		fixResourceData(device, baseData);
		resource.m_fixedData = data;

		HRESULT result = createResourceFunc(device, &data);
		if (FAILED(result))
		{
			data = origData;
			throw HResultException(result);
		}

		if (data.Flags.RenderTarget && !data.Flags.Primary && isVidMemPool(data.Pool))
		{
			device.addRenderTargetResource(baseData);
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
		return m_device.getOrigVtable().pfnLock(m_device, &data);
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
		return m_device.getOrigVtable().pfnUnlock(m_device, &data);
	}
}
