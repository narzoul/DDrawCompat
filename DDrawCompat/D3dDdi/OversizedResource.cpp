#include "D3dDdi/Adapter.h"
#include "D3dDdi/Device.h"
#include "D3dDdi/OversizedResource.h"

namespace D3dDdi
{
	OversizedResource::OversizedResource(Device& device, D3DDDIFORMAT format, const D3DDDI_SURFACEINFO& surfaceInfo)
		: m_device(device)
		, m_format(format)
		, m_surfaceInfo(surfaceInfo)
	{
	}

	HRESULT OversizedResource::blt(D3DDDIARG_BLT& data, HANDLE& resource, RECT& rect)
	{
		const auto& caps = m_device.getAdapter().getD3dExtendedCaps();
		if (rect.right <= static_cast<LONG>(caps.dwMaxTextureWidth) &&
			rect.bottom <= static_cast<LONG>(caps.dwMaxTextureHeight))
		{
			return m_device.getOrigVtable().pfnBlt(m_device.getHandle(), &data);
		}

		HANDLE origResource = resource;
		RECT origRect = rect;
		HANDLE bltResource = createBltResource(rect);

		if (bltResource)
		{
			resource = bltResource;
			rect = RECT{ 0, 0, rect.right - rect.left, rect.bottom - rect.top };
		}

		HRESULT result = m_device.getOrigVtable().pfnBlt(m_device.getHandle(), &data);

		if (bltResource)
		{
			resource = origResource;
			rect = origRect;
			m_device.getOrigVtable().pfnDestroyResource(m_device.getHandle(), bltResource);
		}

		return result;
	}

	HRESULT OversizedResource::bltFrom(D3DDDIARG_BLT data)
	{
		return blt(data, data.hSrcResource, data.SrcRect);
	}

	HRESULT OversizedResource::bltTo(D3DDDIARG_BLT data)
	{
		return blt(data, data.hDstResource, data.DstRect);
	}

	HANDLE OversizedResource::createBltResource(RECT bltRect)
	{
		const RECT surfaceRect = {
			0, 0, static_cast<LONG>(m_surfaceInfo.Width), static_cast<LONG>(m_surfaceInfo.Height) };
		IntersectRect(&bltRect, &surfaceRect, &bltRect);

		D3DDDI_SURFACEINFO bltSurfaceInfo = {};
		bltSurfaceInfo.Width = bltRect.right - bltRect.left;
		bltSurfaceInfo.Height = bltRect.bottom - bltRect.top;
		bltSurfaceInfo.pSysMem = static_cast<const unsigned char*>(m_surfaceInfo.pSysMem) +
			bltRect.top * m_surfaceInfo.SysMemPitch +
			bltRect.left * getBytesPerPixel(m_format);
		bltSurfaceInfo.SysMemPitch = m_surfaceInfo.SysMemPitch;

		D3DDDIARG_CREATERESOURCE2 bltResourceData = {};
		bltResourceData.Format = m_format;
		bltResourceData.Pool = D3DDDIPOOL_SYSTEMMEM;
		bltResourceData.pSurfList = &bltSurfaceInfo;
		bltResourceData.SurfCount = 1;

		if (m_device.getOrigVtable().pfnCreateResource2)
		{
			m_device.getOrigVtable().pfnCreateResource2(m_device.getHandle(), &bltResourceData);
		}
		else
		{
			m_device.getOrigVtable().pfnCreateResource(m_device.getHandle(),
				reinterpret_cast<D3DDDIARG_CREATERESOURCE*>(&bltResourceData));
		}
		return bltResourceData.hResource;
	}

	bool OversizedResource::isSupportedFormat(D3DDDIFORMAT format)
	{
		return 0 != getBytesPerPixel(format);
	}
}
