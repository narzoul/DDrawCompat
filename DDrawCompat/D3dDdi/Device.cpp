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
#include <D3dDdi/KernelModeThunks.h>
#include <D3dDdi/Resource.h>
#include <D3dDdi/ScopedCriticalSection.h>
#include <D3dDdi/ShaderAssembler.h>
#include <DDraw/Surfaces/PrimarySurface.h>
#include <DDraw/ScopedThreadLock.h>
#include <Direct3d/Direct3dDevice.h>
#include <Gdi/DcFunctions.h>

namespace
{
	HANDLE g_gdiResourceHandle = nullptr;
	D3dDdi::Resource* g_gdiResource = nullptr;
	bool g_isConfigUpdatePending = false;
}

namespace D3dDdi
{
	Device::Device(Adapter& adapter, HANDLE device, HANDLE runtimeDevice)
		: m_origVtable(CompatVtable<D3DDDI_DEVICEFUNCS>::s_origVtable)
		, m_adapter(adapter)
		, m_guid(nullptr)
		, m_guidBuf{}
		, m_device(device)
		, m_runtimeDevice(runtimeDevice)
		, m_eventQuery(nullptr)
		, m_isEventQueryPending(false)
		, m_depthStencil(nullptr)
		, m_renderTarget(nullptr)
		, m_renderTargetSubResourceIndex(0)
		, m_sharedPrimary(nullptr)
		, m_drawPrimitive(*this)
		, m_state(*this)
		, m_shaderBlitter(*this)
	{
		D3DDDIARG_CREATEQUERY createQuery = {};
		createQuery.QueryType = D3DDDIQUERYTYPE_EVENT;
		m_origVtable.pfnCreateQuery(m_device, &createQuery);
		m_eventQuery = createQuery.hQuery;
	}

	void Device::add(Adapter& adapter, HANDLE device, HANDLE runtimeDevice)
	{
		s_devices.try_emplace(device, adapter, device, runtimeDevice);
	}

	HRESULT Device::clear(D3DDDIARG_CLEAR data, UINT numRect, const RECT* rect, Resource* resource, DWORD flags)
	{
		if (0 == flags)
		{
			return S_OK;
		}

		data.Flags &= ~(D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER | D3DCLEAR_STENCIL);
		data.Flags |= flags;

		if (resource)
		{
			static std::vector<RECT> scaledRects;
			scaledRects.assign(rect, rect + numRect);
			for (UINT i = 0; i < numRect; ++i)
			{
				resource->scaleRect(scaledRects[i]);
			}
			rect = scaledRects.data();
		}

		return m_origVtable.pfnClear(m_device, &data, numRect, rect);
	}

	HRESULT Device::createPrivateResource(D3DDDIARG_CREATERESOURCE2& data)
	{
		LOG_FUNC("Device::createPrivateResource", data);
		const auto origFormat = data.Format;
		const auto origMipLevels = data.MipLevels;
		const auto origFlags = data.Flags.Value;

		if (D3DDDIFMT_P8 == data.Format)
		{
			data.Format = D3DDDIFMT_L8;
			if (!data.Flags.Texture)
			{
				data.Flags.Texture = 1;
				data.MipLevels = 1;
			}
		}

		HRESULT result = m_origVtable.pfnCreateResource2
			? m_origVtable.pfnCreateResource2(m_device, &data)
			: m_origVtable.pfnCreateResource(m_device, reinterpret_cast<D3DDDIARG_CREATERESOURCE*>(&data));

		data.Format = origFormat;
		data.MipLevels = origMipLevels;
		data.Flags.Value = origFlags;
		return LOG_RESULT(result);
	}

	Device* Device::findDeviceByDd(CompatRef<IDirectDraw7> dd)
	{
		const auto& adapterInfo = KernelModeThunks::getAdapterInfo(dd);
		for (auto& device : s_devices)
		{
			if (adapterInfo.luid == device.second.m_adapter.getLuid() &&
				adapterInfo.vidPnSourceId == device.second.m_adapter.getVidPnSourceId())
			{
				return &device.second;
			}
		}
		return nullptr;
	}

	Device* Device::findDeviceByName(const std::wstring& deviceName)
	{
		for (auto& device : s_devices)
		{
			if (deviceName == device.second.m_adapter.getDeviceName())
			{
				return &device.second;
			}
		}
		return nullptr;
	}

	Device* Device::findDeviceByRuntimeHandle(HANDLE runtimeDevice)
	{
		for (auto& device : s_devices)
		{
			if (runtimeDevice == device.second.m_runtimeDevice)
			{
				return &device.second;
			}
		}
		return nullptr;
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

	void Device::initRepository(GUID* guid)
	{
		if (m_repository)
		{
			return;
		}

		if (guid)
		{
			m_guidBuf = *guid;
			m_guid = &m_guidBuf;
		}

		CompatPtr<IDirectDraw7> repo;
		HRESULT result = CALL_ORIG_PROC(DirectDrawCreateEx)(
			guid, reinterpret_cast<void**>(&repo.getRef()), IID_IDirectDraw7, nullptr);
		if (SUCCEEDED(result))
		{
			result = repo.get()->lpVtbl->SetCooperativeLevel(repo, nullptr, DDSCL_NORMAL | DDSCL_FPUPRESERVE);
		}
		if (FAILED(result))
		{
			LOG_INFO << "ERROR: Failed to create a surface repository: " << Compat::hex(result);
			return;
		}

		m_repository.reset(new SurfaceRepository(repo));
	}

	void Device::prepareForGpuWrite()
	{
		if (m_depthStencil)
		{
			m_depthStencil->prepareForGpuWrite(0);
		}
		if (m_renderTarget)
		{
			m_renderTarget->prepareForGpuWrite(m_renderTargetSubResourceIndex);
		}
	}

	void Device::setDepthStencil(HANDLE resource)
	{
		m_depthStencil = getResource(resource);
	}

	void Device::setGdiResourceHandle(HANDLE resource)
	{
		LOG_FUNC("Device::setGdiResourceHandle", resource);
		ScopedCriticalSection lock;
		if (resource == g_gdiResourceHandle)
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
		prepareForGpuWrite();
		m_state.flush();

		if (0 == numRect || !rect)
		{
			return m_origVtable.pfnClear(m_device, data, numRect, rect);
		}

		HRESULT result = clear(*data, numRect, rect, m_renderTarget, data->Flags & D3DCLEAR_TARGET);
		if (SUCCEEDED(result))
		{
			result = clear(*data, numRect, rect, m_depthStencil, data->Flags & (D3DCLEAR_ZBUFFER | D3DCLEAR_STENCIL));
		}
		return result;
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

	HRESULT Device::pfnCreateVertexShaderFunc(D3DDDIARG_CREATEVERTEXSHADERFUNC* data, const UINT* code)
	{
		LOG_DEBUG << "Vertex shader bytecode: " << Compat::hexDump(code, data->Size);
		LOG_DEBUG << ShaderAssembler(code, data->Size).disassemble();
		return m_origVtable.pfnCreateVertexShaderFunc(m_device, data, code);
	}

	HRESULT Device::pfnDepthFill(const D3DDDIARG_DEPTHFILL* data)
	{
		flushPrimitives();
		auto it = m_resources.find(data->hResource);
		if (it != m_resources.end())
		{
			return it->second->depthFill(*data);
		}
		return m_origVtable.pfnDepthFill(m_device, data);
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

		if (m_renderTarget && resource == *m_renderTarget)
		{
			m_renderTarget = nullptr;
		}
		else if (m_depthStencil && resource == *m_depthStencil)
		{
			m_depthStencil = nullptr;
		}

		HRESULT result = m_origVtable.pfnDestroyResource(m_device, resource);
		if (SUCCEEDED(result))
		{
			auto it = m_resources.find(resource);
			Resource* res = nullptr;
			if (it != m_resources.end())
			{
				res = it->second.get();
				m_resources.erase(it);
			}
			if (resource == m_sharedPrimary)
			{
				m_sharedPrimary = nullptr;
			}
			if (resource == g_gdiResourceHandle)
			{
				g_gdiResourceHandle = nullptr;
				g_gdiResource = nullptr;
				DDraw::PrimarySurface::onLost();
			}
			m_drawPrimitive.removeSysMemVertexBuffer(resource);
			m_state.onDestroyResource(res, resource);
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
		HRESULT result = m_drawPrimitive.draw(*data, flagBuffer);
		if (D3DPT_POINTLIST == data->PrimitiveType)
		{
			Direct3d::Direct3dDevice::drawExecuteBufferPointPatches([&](const D3DPOINT& points)
				{
					if (SUCCEEDED(result))
					{
						D3DDDIARG_DRAWPRIMITIVE dp = *data;
						dp.VStart = points.wFirst;
						dp.PrimitiveCount = points.wCount;
						LOG_FUNC("_D3DDDI_DEVICEFUNCS::pfnDrawPrimitive[patch]", &dp, flagBuffer);
						result = m_drawPrimitive.draw(dp, flagBuffer);
						LOG_RESULT(result);
					}
				});
		}
		return result;
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

		Gdi::DcFunctions::disableDibRedirection(true);
		HRESULT result = m_origVtable.pfnPresent(m_device, &d);
		Gdi::DcFunctions::disableDibRedirection(false);
		updateAllConfigNow();
		return result;
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

		Gdi::DcFunctions::disableDibRedirection(true);
		HRESULT result = m_origVtable.pfnPresent1(m_device, data);
		Gdi::DcFunctions::disableDibRedirection(false);
		updateAllConfigNow();
		return result;
	}

	HRESULT Device::pfnSetPalette(const D3DDDIARG_SETPALETTE* data)
	{
		flushPrimitives();
		if (data->PaletteHandle >= m_palettes.size())
		{
			m_palettes.resize(data->PaletteHandle + 1);
		}

		auto it = m_resources.find(data->hResource);
		if (it != m_resources.end())
		{
			it->second->setPaletteHandle(data->PaletteHandle);
		}
		return S_OK;
	}

	HRESULT Device::pfnTexBlt(const D3DDDIARG_TEXBLT* data)
	{
		prepareForTextureBlt(data->hDstResource, data->hSrcResource);
		return m_origVtable.pfnTexBlt(m_device, data);
	}

	HRESULT Device::pfnTexBlt1(const D3DDDIARG_TEXBLT1* data)
	{
		prepareForTextureBlt(data->hDstResource, data->hSrcResource);
		return m_origVtable.pfnTexBlt1(m_device, data);
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

	HRESULT Device::pfnUpdatePalette(const D3DDDIARG_UPDATEPALETTE* data, const PALETTEENTRY* paletteData)
	{
		LOG_DEBUG << Compat::array(reinterpret_cast<const HANDLE*>(paletteData), data->NumEntries);
		flushPrimitives();
		if (data->PaletteHandle >= m_palettes.size())
		{
			m_palettes.resize(data->PaletteHandle + 1);
		}

		for (UINT i = 0; i < data->NumEntries; ++i)
		{
			auto& rgbQuad = m_palettes[data->PaletteHandle][data->StartIndex + i];
			rgbQuad.rgbReserved = 0xFF;
			rgbQuad.rgbRed = paletteData[i].peRed;
			rgbQuad.rgbGreen = paletteData[i].peGreen;
			rgbQuad.rgbBlue = paletteData[i].peBlue;
		}

		for (auto& resourcePair : m_resources)
		{
			if (resourcePair.second->getPaletteHandle() == data->PaletteHandle)
			{
				resourcePair.second->invalidatePalettizedTexture();
			}
		}
		return S_OK;
	}

	HRESULT Device::pfnValidateDevice(D3DDDIARG_VALIDATETEXTURESTAGESTATE* data)
	{
		m_state.flush();
		return m_origVtable.pfnValidateDevice(m_device, data);
	}

	void Device::prepareForTextureBlt(HANDLE dstResource, HANDLE srcResource)
	{
		flushPrimitives();
		auto it = m_resources.find(dstResource);
		if (it != m_resources.end())
		{
			it->second->prepareForGpuWriteAll();
		}

		it = m_resources.find(srcResource);
		if (it != m_resources.end())
		{
			it->second->prepareForGpuReadAll();
		}
	}

	void Device::updateAllConfig()
	{
		g_isConfigUpdatePending = true;
	}

	void Device::updateAllConfigNow()
	{
		if (g_isConfigUpdatePending)
		{
			g_isConfigUpdatePending = false;
			for (auto& device : s_devices)
			{
				device.second.updateConfig();
			}
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

	void Device::waitForIdle()
	{
		D3dDdi::ScopedCriticalSection lock;
		flushPrimitives();

		if (m_origVtable.pfnFlush1)
		{
			m_origVtable.pfnFlush1(m_device, 0);
		}
		else
		{
			m_origVtable.pfnFlush(m_device);
		}

		if (m_isEventQueryPending)
		{
			BOOL result = FALSE;
			D3DDDIARG_GETQUERYDATA getQueryData = {};
			getQueryData.hQuery = m_eventQuery;
			getQueryData.pData = &result;
			while (S_FALSE == m_origVtable.pfnGetQueryData(m_device, &getQueryData))
			{
			}
		}

		D3DDDIARG_ISSUEQUERY issueQuery = {};
		issueQuery.hQuery = m_eventQuery;
		issueQuery.Flags.End = 1;
		m_isEventQueryPending = SUCCEEDED(m_origVtable.pfnIssueQuery(m_device, &issueQuery));
	}

	std::map<HANDLE, Device> Device::s_devices;
	bool Device::s_isFlushEnabled = true;
}
