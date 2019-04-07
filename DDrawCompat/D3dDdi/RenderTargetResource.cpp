#include "D3dDdi/Device.h"
#include "D3dDdi/RenderTargetResource.h"

namespace D3dDdi
{
	RenderTargetResource::RenderTargetResource(Device& device, HANDLE resource, D3DDDIFORMAT format, UINT surfaceCount)
		: m_device(device)
		, m_resource(resource)
		, m_bytesPerPixel(getBytesPerPixel(format))
		, m_subResources(surfaceCount, SubResource(*this))
	{
	}

	HRESULT RenderTargetResource::lock(D3DDDIARG_LOCK& data)
	{
		if (data.SubResourceIndex >= m_subResources.size())
		{
			return m_device.getOrigVtable().pfnLock(m_device.getHandle(), &data);
		}

		auto& subResource = m_subResources[data.SubResourceIndex];
		if (subResource.surfacePtr)
		{
			auto surfacePtr = static_cast<unsigned char*>(subResource.surfacePtr);
			if (data.Flags.AreaValid)
			{
				surfacePtr += data.Area.top * subResource.pitch + data.Area.left * m_bytesPerPixel;
			}
			data.pSurfData = surfacePtr;
			data.Pitch = subResource.pitch;
			subResource.isLocked = true;
			return S_OK;
		}

		const UINT origFlags = data.Flags.Value;
		data.Flags.Value = 0;
		const HRESULT result = m_device.getOrigVtable().pfnLock(m_device.getHandle(), &data);
		data.Flags.Value = origFlags;

		if (SUCCEEDED(result))
		{
			subResource.surfacePtr = data.pSurfData;
			subResource.pitch = data.Pitch;
			subResource.isLocked = true;
			m_lockedSubResources.insert(data.SubResourceIndex);
		}

		return result;
	}

	HRESULT RenderTargetResource::unlock(const D3DDDIARG_UNLOCK& data)
	{
		if (data.SubResourceIndex >= m_subResources.size())
		{
			return m_device.getOrigVtable().pfnUnlock(m_device.getHandle(), &data);
		}

		m_subResources[data.SubResourceIndex].isLocked = false;
		return S_OK;
	}

	void RenderTargetResource::prepareForRendering(UINT subResourceIndex)
	{
		if (UINT_MAX == subResourceIndex)
		{
			auto it = m_lockedSubResources.begin();
			while (it != m_lockedSubResources.end())
			{
				prepareSubResourceForRendering(*(it++));
			}
		}

		if (subResourceIndex >= m_subResources.size())
		{
			return;
		}

		prepareSubResourceForRendering(subResourceIndex);
	}

	void RenderTargetResource::prepareSubResourceForRendering(UINT subResourceIndex)
	{
		auto& subResource = m_subResources[subResourceIndex];
		if (subResource.surfacePtr && !subResource.isLocked)
		{
			D3DDDIARG_UNLOCK data = {};
			data.hResource = m_resource;
			data.SubResourceIndex = subResourceIndex;
			m_device.getOrigVtable().pfnUnlock(m_device.getHandle(), &data);

			subResource.surfacePtr = nullptr;
			subResource.pitch = 0;
			m_lockedSubResources.erase(subResourceIndex);
		}
	}

	RenderTargetResource::SubResource::SubResource(RenderTargetResource& parent)
		: parent(parent)
		, surfacePtr(nullptr)
		, pitch(0)
		, isLocked(false)
	{
	}
}
