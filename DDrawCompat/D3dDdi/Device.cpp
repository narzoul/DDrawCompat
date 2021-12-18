#include <sstream>

#include <d3d.h>
#include <winternl.h>
#include <d3dkmthk.h>

#include <Common/CompatVtable.h>
#include <Common/HResultException.h>
#include <Common/Log.h>
#include <D3dDdi/Adapter.h>
#include <D3dDdi/Device.h>
#include <D3dDdi/DeviceFuncs.h>
#include <D3dDdi/Resource.h>
#include <D3dDdi/ScopedCriticalSection.h>
#include <DDraw/ScopedThreadLock.h>

namespace
{
	HANDLE g_gdiResourceHandle = nullptr;
	D3dDdi::Resource* g_gdiResource = nullptr;
}

namespace D3dDdi
{
	Device::Device(Adapter& adapter, HANDLE device)
		: m_origVtable(CompatVtable<D3DDDI_DEVICEFUNCS>::s_origVtable)
		, m_adapter(adapter)
		, m_device(device)
		, m_renderTarget(nullptr)
		, m_renderTargetSubResourceIndex(0)
		, m_sharedPrimary(nullptr)
		, m_drawPrimitive(*this)
		, m_state(*this)
		, m_shaderBlitter(*this)
	{
	}

	void Device::add(Adapter& adapter, HANDLE device)
	{
		s_devices.try_emplace(device, adapter, device);
	}

	HRESULT Device::createPrivateResource(D3DDDIARG_CREATERESOURCE2& data)
	{
		const bool isPalettizedOffScreen = D3DDDIFMT_P8 == data.Format && !data.Flags.Texture;
		if (isPalettizedOffScreen)
		{
			data.Format = D3DDDIFMT_L8;
			data.Flags.Texture = 1;
		}

		HRESULT result = m_origVtable.pfnCreateResource2
			? m_origVtable.pfnCreateResource2(m_device, &data)
			: m_origVtable.pfnCreateResource(m_device, reinterpret_cast<D3DDDIARG_CREATERESOURCE*>(&data));

		if (isPalettizedOffScreen)
		{
			data.Format = D3DDDIFMT_P8;
			data.Flags.Texture = 0;
		}

		return result;
	}

	Device* Device::findDeviceByResource(HANDLE resource)
	{
		for (auto& device : s_devices)
		{
			if (device.second.m_resources.find(resource) != device.second.m_resources.end())
			{
				return &device.second;
			}
		}
		return nullptr;
	}

	Resource* Device::findResource(HANDLE resource)
	{
		for (auto& device : s_devices)
		{
			auto res = device.second.getResource(resource);
			if (res)
			{
				return res;
			}
		}
		return nullptr;
	}

	Resource* Device::getGdiResource()
	{
		return g_gdiResource;
	}

	Resource* Device::getResource(HANDLE resource)
	{
		auto it = m_resources.find(resource);
		return it != m_resources.end() ? it->second.get() : nullptr;
	}

	void Device::prepareForGpuWrite()
	{
		if (m_renderTarget)
		{
			m_renderTarget->prepareForGpuWrite(m_renderTargetSubResourceIndex);
		}
	}

	void Device::setGdiResourceHandle(HANDLE resource)
	{
		ScopedCriticalSection lock;
		if ((!resource && !g_gdiResource) ||
			(g_gdiResource && resource == *g_gdiResource))
		{
			return;
		}

		if (g_gdiResource)
		{
			g_gdiResource->setAsGdiResource(false);
		}

		g_gdiResourceHandle = resource;
		g_gdiResource = findResource(resource);

		if (g_gdiResource)
		{
			g_gdiResource->setAsGdiResource(true);
		}
	}

	void Device::setRenderTarget(const D3DDDIARG_SETRENDERTARGET& data)
	{
		if (0 == data.RenderTargetIndex)
		{
			m_renderTarget = getResource(data.hRenderTarget);
			m_renderTargetSubResourceIndex = data.SubResourceIndex;
		}
	}

	HRESULT Device::pfnBlt(const D3DDDIARG_BLT* data)
	{
		flushPrimitives();
		auto it = m_resources.find(data->hDstResource);
		if (it != m_resources.end())
		{
			return it->second->blt(*data);
		}

		it = m_resources.find(data->hSrcResource);
		if (it != m_resources.end())
		{
			it->second->prepareForBltSrc(*data);
		}
		return m_origVtable.pfnBlt(m_device, data);
	}

	HRESULT Device::pfnClear(const D3DDDIARG_CLEAR* data, UINT numRect, const RECT* rect)
	{
		flushPrimitives();
		m_state.flush();
		if (data->Flags & D3DCLEAR_TARGET)
		{
			prepareForGpuWrite();
		}
		return m_origVtable.pfnClear(m_device, data, numRect, rect);
	}

	HRESULT Device::pfnColorFill(const D3DDDIARG_COLORFILL* data)
	{
		flushPrimitives();
		auto it = m_resources.find(data->hResource);
		if (it != m_resources.end())
		{
			return it->second->colorFill(*data);
		}
		return m_origVtable.pfnColorFill(m_device, data);
	}

	HRESULT Device::pfnCreateResource(D3DDDIARG_CREATERESOURCE* data)
	{
		D3DDDIARG_CREATERESOURCE2 data2 = {};
		memcpy(&data2, data, sizeof(*data));
		HRESULT result = pfnCreateResource2(&data2);
		data->hResource = data2.hResource;
		return result;
	}

	HRESULT Device::pfnCreateResource2(D3DDDIARG_CREATERESOURCE2* data)
	{
		try
		{
			auto resource(std::make_unique<Resource>(*this, *data));
			m_resources.emplace(*resource, std::move(resource));
			if (data->Flags.VertexBuffer &&
				D3DDDIPOOL_SYSTEMMEM == data->Pool &&
				data->pSurfList[0].pSysMem)
			{
				m_drawPrimitive.addSysMemVertexBuffer(data->hResource,
					static_cast<BYTE*>(const_cast<void*>(data->pSurfList[0].pSysMem)));
			}
			return S_OK;
		}
		catch (const HResultException& e)
		{
			return e.getResult();
		}
	}

	HRESULT Device::pfnDestroyDevice()
	{
		auto device = m_device;
		auto pfnDestroyDevice = m_origVtable.pfnDestroyDevice;
		s_devices.erase(device);
		return pfnDestroyDevice(device);
	}

	HRESULT Device::pfnDestroyResource(HANDLE resource)
	{
		flushPrimitives();
		if (g_gdiResource)
		{
			g_gdiResource->onDestroyResource(resource);
		}

		if (resource == m_sharedPrimary)
		{
			D3DKMTReleaseProcessVidPnSourceOwners(GetCurrentProcess());
		}

		HRESULT result = m_origVtable.pfnDestroyResource(m_device, resource);
		if (SUCCEEDED(result))
		{
			m_resources.erase(resource);
			if (resource == m_sharedPrimary)
			{
				m_sharedPrimary = nullptr;
			}
			if (resource == g_gdiResourceHandle)
			{
				g_gdiResourceHandle = nullptr;
				g_gdiResource = nullptr;
			}
			m_drawPrimitive.removeSysMemVertexBuffer(resource);
			m_state.onDestroyResource(resource);
		}

		return result;
	}

	HRESULT Device::pfnDrawIndexedPrimitive2(const D3DDDIARG_DRAWINDEXEDPRIMITIVE2* data,
		UINT /*indicesSize*/, const void* indexBuffer, const UINT* flagBuffer)
	{
		return m_drawPrimitive.drawIndexed(*data, static_cast<const UINT16*>(indexBuffer), flagBuffer);
	}

	HRESULT Device::pfnDrawPrimitive(const D3DDDIARG_DRAWPRIMITIVE* data, const UINT* flagBuffer)
	{
		return m_drawPrimitive.draw(*data, flagBuffer);
	}

	HRESULT Device::pfnFlush()
	{
		if (!s_isFlushEnabled)
		{
			return S_OK;
		}
		flushPrimitives();
		return m_origVtable.pfnFlush(m_device);
	}

	HRESULT Device::pfnFlush1(UINT FlushFlags)
	{
		if (!s_isFlushEnabled && 0 == FlushFlags)
		{
			return S_OK;
		}
		flushPrimitives();
		return m_origVtable.pfnFlush1(m_device, FlushFlags);
	}

	HRESULT Device::pfnLock(D3DDDIARG_LOCK* data)
	{
		flushPrimitives();
		auto it = m_resources.find(data->hResource);
		if (it != m_resources.end())
		{
			return it->second->lock(*data);
		}
		return m_origVtable.pfnLock(m_device, data);
	}

	HRESULT Device::pfnOpenResource(D3DDDIARG_OPENRESOURCE* data)
	{
		HRESULT result = m_origVtable.pfnOpenResource(m_device, data);
		if (SUCCEEDED(result) && data->Flags.Fullscreen)
		{
			m_sharedPrimary = data->hResource;
		}
		return result;
	}

	HRESULT Device::pfnPresent(const D3DDDIARG_PRESENT* data)
	{
		flushPrimitives();
		auto d = *data;
		auto resource = getResource(data->hSrcResource);
		if (resource)
		{
			d.hSrcResource = resource->prepareForGpuRead(data->SrcSubResourceIndex);
		}
		return m_origVtable.pfnPresent(m_device, &d);
	}

	HRESULT Device::pfnPresent1(D3DDDIARG_PRESENT1* data)
	{
		flushPrimitives();
		std::vector<D3DDDIARG_PRESENTSURFACE> srcResources(data->phSrcResources, data->phSrcResources + data->SrcResources);
		data->phSrcResources = srcResources.data();
		for (UINT i = 0; i < data->SrcResources; ++i)
		{
			auto resource = getResource(srcResources[i].hResource);
			if (resource)
			{
				srcResources[i].hResource = resource->prepareForGpuRead(srcResources[i].SubResourceIndex);
			}
		}
		return m_origVtable.pfnPresent1(m_device, data);
	}

	HRESULT Device::pfnUnlock(const D3DDDIARG_UNLOCK* data)
	{
		flushPrimitives();
		auto it = m_resources.find(data->hResource);
		if (it != m_resources.end())
		{
			return it->second->unlock(*data);
		}
		return m_origVtable.pfnUnlock(m_device, data);
	}

	void Device::updateAllConfig()
	{
		DDraw::ScopedThreadLock ddLock;
		D3dDdi::ScopedCriticalSection lock;
		for (auto& device : s_devices)
		{
			device.second.updateConfig();
		}
	}

	void Device::updateConfig()
	{
		for (auto& resource : m_resources)
		{
			resource.second->updateConfig();
		}
		m_state.updateConfig();
	}

	std::map<HANDLE, Device> Device::s_devices;
	bool Device::s_isFlushEnabled = true;
}
