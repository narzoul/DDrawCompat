#include <d3d.h>
#include <../km/d3dkmthk.h>

#include "D3dDdi/AdapterFuncs.h"
#include "D3dDdi/Device.h"
#include "D3dDdi/DeviceFuncs.h"
#include "D3dDdi/KernelModeThunks.h"
#include "Gdi/AccessGuard.h"

namespace
{
	D3DDDI_RESOURCEFLAGS getResourceTypeFlags();

	const UINT g_resourceTypeFlags = getResourceTypeFlags().Value;
	HANDLE g_gdiResourceHandle = nullptr;
	bool g_isReadOnlyGdiLockEnabled = false;

	class RenderGuard : public Gdi::DDrawAccessGuard
	{
	public:
		RenderGuard(D3dDdi::Device& device, Gdi::Access access)
			: Gdi::DDrawAccessGuard(access)
			, m_device(device)
		{
			device.prepareForRendering();
		}

		RenderGuard(D3dDdi::Device& device, Gdi::Access access, HANDLE resource, UINT subResourceIndex = UINT_MAX)
			: Gdi::DDrawAccessGuard(access, g_gdiResourceHandle == resource)
			, m_device(device)
		{
			device.prepareForRendering(resource, subResourceIndex);
		}

	private:
		D3dDdi::Device & m_device;
	};

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
}

namespace D3dDdi
{
	UINT getBytesPerPixel(D3DDDIFORMAT format)
	{
		switch (format)
		{
		case D3DDDIFMT_A8:
		case D3DDDIFMT_P8:
			return 1;

		case D3DDDIFMT_R5G6B5:
		case D3DDDIFMT_X1R5G5B5:
		case D3DDDIFMT_A1R5G5B5:
		case D3DDDIFMT_A8P8:
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

	Device::Device(HANDLE adapter, HANDLE device)
		: m_origVtable(&DeviceFuncs::s_origVtables.at(device))
		, m_adapter(adapter)
		, m_device(device)
		, m_sharedPrimary(nullptr)
	{
	}

	HRESULT Device::blt(const D3DDDIARG_BLT& data)
	{
		RenderGuard srcRenderGuard(*this, Gdi::ACCESS_READ, data.hSrcResource, data.SrcSubResourceIndex);
		RenderGuard dstRenderGuard(*this, Gdi::ACCESS_WRITE, data.hDstResource, data.DstSubResourceIndex);

		auto it = m_oversizedResources.find(data.hSrcResource);
		if (it != m_oversizedResources.end())
		{
			return it->second.bltFrom(data);
		}

		it = m_oversizedResources.find(data.hDstResource);
		if (it != m_oversizedResources.end())
		{
			return it->second.bltTo(data);
		}

		return m_origVtable->pfnBlt(m_device, &data);
	}

	HRESULT Device::clear(const D3DDDIARG_CLEAR& data, UINT numRect, const RECT* rect)
	{
		RenderGuard renderGuard(*this, Gdi::ACCESS_WRITE);
		return m_origVtable->pfnClear(m_device, &data, numRect, rect);
	}

	HRESULT Device::colorFill(const D3DDDIARG_COLORFILL& data)
	{
		RenderGuard renderGuard(*this, Gdi::ACCESS_WRITE, data.hResource, data.SubResourceIndex);
		return m_origVtable->pfnColorFill(m_device, &data);
	}

	template <typename CreateResourceArg, typename CreateResourceFunc>
	HRESULT Device::createOversizedResource(
		CreateResourceArg& data,
		CreateResourceFunc origCreateResource,
		const D3DNTHAL_D3DEXTENDEDCAPS& caps)
	{
		D3DDDI_SURFACEINFO compatSurfaceInfo = data.pSurfList[0];
		if (0 != caps.dwMaxTextureWidth && compatSurfaceInfo.Width > caps.dwMaxTextureWidth)
		{
			compatSurfaceInfo.Width = caps.dwMaxTextureWidth;
		}
		if (0 != caps.dwMaxTextureHeight && compatSurfaceInfo.Height > caps.dwMaxTextureHeight)
		{
			compatSurfaceInfo.Height = caps.dwMaxTextureHeight;
		}

		const D3DDDI_SURFACEINFO* origSurfList = data.pSurfList;
		data.pSurfList = &compatSurfaceInfo;
		HRESULT result = origCreateResource(m_device, &data);
		data.pSurfList = origSurfList;

		if (SUCCEEDED(result))
		{
			m_oversizedResources.emplace(data.hResource,
				OversizedResource(*m_origVtable, m_adapter, m_device, data.Format, origSurfList[0]));
		}

		return result;
	}

	template <typename CreateResourceArg, typename CreateResourceFunc>
	HRESULT Device::createResourceImpl(CreateResourceArg& data, CreateResourceFunc origCreateResource)
	{
		const bool isOffScreenPlain = 0 == (data.Flags.Value & g_resourceTypeFlags);
		if (D3DDDIPOOL_SYSTEMMEM == data.Pool &&
			(isOffScreenPlain || data.Flags.Texture) &&
			OversizedResource::isSupportedFormat(data.Format) &&
			1 == data.SurfCount &&
			m_adapter)
		{
			const auto& caps = AdapterFuncs::getD3dExtendedCaps(m_adapter);
			const auto& surfaceInfo = data.pSurfList[0];
			if (0 != caps.dwMaxTextureWidth && surfaceInfo.Width > caps.dwMaxTextureWidth ||
				0 != caps.dwMaxTextureHeight && surfaceInfo.Height > caps.dwMaxTextureHeight)
			{
				return createOversizedResource(data, origCreateResource, caps);
			}
		}

		if (data.Flags.Primary)
		{
			data.Format = D3DDDIFMT_X8R8G8B8;
		}

		HRESULT result = origCreateResource(m_device, &data);
		if (SUCCEEDED(result) && data.Flags.RenderTarget && !data.Flags.Primary && isVidMemPool(data.Pool))
		{
			m_renderTargetResources.emplace(data.hResource,
				RenderTargetResource(*m_origVtable, m_device, data.hResource, data.Format, data.SurfCount));
		}

		return result;
	}

	HRESULT Device::createResource(D3DDDIARG_CREATERESOURCE& data)
	{
		return createResourceImpl(data, m_origVtable->pfnCreateResource);
	}

	HRESULT Device::createResource2(D3DDDIARG_CREATERESOURCE2& data)
	{
		return createResourceImpl(data, m_origVtable->pfnCreateResource2);
	}

	HRESULT Device::destroyResource(HANDLE resource)
	{
		if (resource == m_sharedPrimary)
		{
			D3DKMTReleaseProcessVidPnSourceOwners(GetCurrentProcess());
		}

		HRESULT result = m_origVtable->pfnDestroyResource(m_device, resource);
		if (SUCCEEDED(result))
		{
			m_oversizedResources.erase(resource);
			m_renderTargetResources.erase(resource);
			m_lockedRenderTargetResources.erase(resource);

			if (resource == m_sharedPrimary)
			{
				m_sharedPrimary = nullptr;
			}
			if (resource == g_gdiResourceHandle)
			{
				g_gdiResourceHandle = nullptr;
			}
		}

		return result;
	}

	HRESULT Device::drawIndexedPrimitive(const D3DDDIARG_DRAWINDEXEDPRIMITIVE& data)
	{
		RenderGuard renderGuard(*this, Gdi::ACCESS_WRITE);
		return m_origVtable->pfnDrawIndexedPrimitive(m_device, &data);
	}

	HRESULT Device::drawIndexedPrimitive2(const D3DDDIARG_DRAWINDEXEDPRIMITIVE2& data,
		UINT indicesSize, const void* indexBuffer, const UINT* flagBuffer)
	{
		RenderGuard renderGuard(*this, Gdi::ACCESS_WRITE);
		return m_origVtable->pfnDrawIndexedPrimitive2(m_device, &data, indicesSize, indexBuffer, flagBuffer);
	}

	HRESULT Device::drawPrimitive(const D3DDDIARG_DRAWPRIMITIVE& data, const UINT* flagBuffer)
	{
		RenderGuard renderGuard(*this, Gdi::ACCESS_WRITE);
		return m_origVtable->pfnDrawPrimitive(m_device, &data, flagBuffer);
	}

	HRESULT Device::drawPrimitive2(const D3DDDIARG_DRAWPRIMITIVE2& data)
	{
		RenderGuard renderGuard(*this, Gdi::ACCESS_WRITE);
		return m_origVtable->pfnDrawPrimitive2(m_device, &data);
	}

	HRESULT Device::drawRectPatch(const D3DDDIARG_DRAWRECTPATCH& data, const D3DDDIRECTPATCH_INFO* info,
		const FLOAT* patch)
	{
		RenderGuard renderGuard(*this, Gdi::ACCESS_WRITE);
		return m_origVtable->pfnDrawRectPatch(m_device, &data, info, patch);
	}

	HRESULT Device::drawTriPatch(const D3DDDIARG_DRAWTRIPATCH& data, const D3DDDITRIPATCH_INFO* info,
		const FLOAT* patch)
	{
		RenderGuard renderGuard(*this, Gdi::ACCESS_WRITE);
		return m_origVtable->pfnDrawTriPatch(m_device, &data, info, patch);
	}

	HRESULT Device::lock(D3DDDIARG_LOCK& data)
	{
		Gdi::DDrawAccessGuard accessGuard(
			(data.Flags.ReadOnly || g_isReadOnlyGdiLockEnabled) ? Gdi::ACCESS_READ : Gdi::ACCESS_WRITE,
			data.hResource == g_gdiResourceHandle);

		auto it = m_renderTargetResources.find(data.hResource);
		if (it != m_renderTargetResources.end())
		{
			HRESULT result = it->second.lock(data);
			if (SUCCEEDED(result))
			{
				m_lockedRenderTargetResources.emplace(it->first, it->second);
			}
			return result;
		}
		return m_origVtable->pfnLock(m_device, &data);
	}

	HRESULT Device::openResource(D3DDDIARG_OPENRESOURCE& data)
	{
		HRESULT result = m_origVtable->pfnOpenResource(m_device, &data);
		if (SUCCEEDED(result) && data.Flags.Fullscreen)
		{
			m_sharedPrimary = data.hResource;
		}
		return result;
	}

	HRESULT Device::present(const D3DDDIARG_PRESENT& data)
	{
		RenderGuard renderGuard(*this, Gdi::ACCESS_READ, data.hSrcResource, data.SrcSubResourceIndex);
		return m_origVtable->pfnPresent(m_device, &data);
	}

	HRESULT Device::present1(D3DDDIARG_PRESENT1& data)
	{
		bool isGdiResourceInvolved = false;
		for (UINT i = 0; i < data.SrcResources && !isGdiResourceInvolved; ++i)
		{
			isGdiResourceInvolved = data.phSrcResources[i].hResource == g_gdiResourceHandle;
		}

		Gdi::DDrawAccessGuard accessGuard(Gdi::ACCESS_READ, isGdiResourceInvolved);

		for (UINT i = 0; i < data.SrcResources; ++i)
		{
			prepareForRendering(data.phSrcResources[i].hResource, data.phSrcResources[i].SubResourceIndex);
		}

		return m_origVtable->pfnPresent1(m_device, &data);
	}

	HRESULT Device::texBlt(const D3DDDIARG_TEXBLT& data)
	{
		RenderGuard dstRenderGuard(*this, Gdi::ACCESS_WRITE, data.hDstResource);
		RenderGuard srcRenderGuard(*this, Gdi::ACCESS_READ, data.hSrcResource);
		return m_origVtable->pfnTexBlt(m_device, &data);
	}

	HRESULT Device::texBlt1(const D3DDDIARG_TEXBLT1& data)
	{
		RenderGuard dstRenderGuard(*this, Gdi::ACCESS_WRITE, data.hDstResource);
		RenderGuard srcRenderGuard(*this, Gdi::ACCESS_READ, data.hSrcResource);
		return m_origVtable->pfnTexBlt1(m_device, &data);
	}

	HRESULT Device::unlock(const D3DDDIARG_UNLOCK& data)
	{
		Gdi::DDrawAccessGuard accessGuard(Gdi::ACCESS_READ, data.hResource == g_gdiResourceHandle);
		auto it = m_renderTargetResources.find(data.hResource);
		if (it != m_renderTargetResources.end())
		{
			return it->second.unlock(data);
		}
		return m_origVtable->pfnUnlock(m_device, &data);
	}

	HRESULT Device::updateWInfo(const D3DDDIARG_WINFO& data)
	{
		if (1.0f == data.WNear && 1.0f == data.WFar)
		{
			D3DDDIARG_WINFO wInfo = {};
			wInfo.WNear = 0.0f;
			wInfo.WFar = 1.0f;
			return m_origVtable->pfnUpdateWInfo(m_device, &wInfo);
		}
		return m_origVtable->pfnUpdateWInfo(m_device, &data);
	}

	void Device::prepareForRendering(RenderTargetResource& resource, UINT subResourceIndex)
	{
		resource.prepareForRendering(subResourceIndex);
		if (!resource.hasLockedSubResources())
		{
			m_lockedRenderTargetResources.erase(resource.getHandle());
		}
	}

	void Device::prepareForRendering(HANDLE resource, UINT subResourceIndex)
	{
		auto it = m_lockedRenderTargetResources.find(resource);
		if (it != m_lockedRenderTargetResources.end())
		{
			prepareForRendering(it->second, subResourceIndex);
		}
	}

	void Device::prepareForRendering()
	{
		auto it = m_lockedRenderTargetResources.begin();
		while (it != m_lockedRenderTargetResources.end())
		{
			prepareForRendering((it++)->second);
		}
	}

	void Device::setGdiResourceHandle(HANDLE resource)
	{
		g_gdiResourceHandle = resource;
	}

	void Device::setReadOnlyGdiLock(bool enable)
	{
		g_isReadOnlyGdiLockEnabled = enable;
	}
}
