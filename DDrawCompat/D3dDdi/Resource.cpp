#include <type_traits>

#include <Common/Comparison.h>
#include <Common/HResultException.h>
#include <Common/Log.h>
#include <Common/Rect.h>
#include <Common/Time.h>
#include <Config/Settings/BltFilter.h>
#include <Config/Settings/ColorKeyMethod.h>
#include <Config/Settings/DepthFormat.h>
#include <Config/Settings/ResolutionScaleFilter.h>
#include <Config/Settings/SurfacePatches.h>
#include <D3dDdi/Adapter.h>
#include <D3dDdi/Device.h>
#include <D3dDdi/KernelModeThunks.h>
#include <D3dDdi/Log/DeviceFuncsLog.h>
#include <D3dDdi/Resource.h>
#include <D3dDdi/ScopedCriticalSection.h>
#include <D3dDdi/SurfaceRepository.h>
#include <DDraw/Blitter.h>
#include <DDraw/RealPrimarySurface.h>
#include <DDraw/Surfaces/PrimarySurface.h>
#include <DDraw/Surfaces/SurfaceImpl.h>
#include <Dll/Dll.h>
#include <Gdi/Cursor.h>
#include <Gdi/Palette.h>
#include <Gdi/VirtualScreen.h>
#include <Gdi/Window.h>
#include <Overlay/Steam.h>

namespace
{
	D3DDDI_RESOURCEFLAGS getResourceTypeFlags();

	const UINT g_resourceTypeFlags = getResourceTypeFlags().Value;
	
	bool g_enableConfig = true;
	D3DDDIFORMAT g_formatOverride = D3DDDIFMT_UNKNOWN;
	std::pair<D3DDDIMULTISAMPLE_TYPE, UINT> g_msaaOverride = {};
	bool g_readOnlyLock = false;

	LONG divCeil(LONG n, LONG d)
	{
		return (n + d - 1) / d;
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
		flags.MatchGdiPrimary = 1;
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

	void heapFree(void* p)
	{
		HeapFree(GetProcessHeap(), 0, p);
	}

	bool isDepthTexture(D3DDDIFORMAT format)
	{
		return D3dDdi::FOURCC_INTZ == format || D3dDdi::FOURCC_DF16 == format;
	}

	void logUnsupportedMsaaDepthBufferResolve()
	{
		LOG_ONCE("Warning: Resolving multisampled depth buffers is not supported by the GPU driver. "
			"Disable antialiasing if experiencing visual glitches.");
	}
}

namespace D3dDdi
{
	Resource::Data::Data(const D3DDDIARG_CREATERESOURCE2& data)
		: D3DDDIARG_CREATERESOURCE2(data)
		, surfaceData(data.pSurfList, data.pSurfList + data.SurfCount)
	{
		pSurfList = surfaceData.data();
	}

	Resource::Resource(Device& device, D3DDDIARG_CREATERESOURCE2& data)
		: m_device(device)
		, m_handle(nullptr)
		, m_origData(data)
		, m_fixedData(data)
		, m_formatInfo{}
		, m_formatOp{}
		, m_lockBuffer(nullptr, &heapFree)
		, m_lockResource(nullptr, ResourceDeleter(device, device.getOrigVtable().pfnDestroyResource))
		, m_lockRefSurface{}
		, m_msaaSurface{}
		, m_msaaResolvedSurface{}
		, m_nullRt{}
		, m_colorKeyedSurface{}
		, m_colorKey(0)
		, m_formatConfig(D3DDDIFMT_UNKNOWN)
		, m_multiSampleConfig{ D3DDDIMULTISAMPLE_NONE, 0 }
		, m_scaledSize{}
		, m_paletteHandle(0)
		, m_isOversized(false)
		, m_isSurfaceRepoResource(SurfaceRepository::inCreateSurface() || !g_enableConfig)
		, m_isClampable(true)
		, m_isPrimary(false)
		, m_isPrimaryScalingNeeded(false)
	{
		if (m_origData.Flags.VertexBuffer &&
			m_origData.Flags.MightDrawFromLocked &&
			D3DDDIPOOL_SYSTEMMEM != m_origData.Pool)
		{
			throw HResultException(E_FAIL);
		}

		fixResourceData();
		m_formatInfo = getFormatInfo(m_fixedData.Format);
		m_formatConfig = m_fixedData.Format;
		m_scaledSize = { static_cast<LONG>(m_fixedData.pSurfList[0].Width), static_cast<LONG>(m_fixedData.pSurfList[0].Height) };

		const auto& formatOps = m_device.getAdapter().getInfo().formatOps;
		auto it = formatOps.find(m_fixedData.Format);
		if (it != formatOps.end())
		{
			m_formatOp = it->second;
		}

		HRESULT result = m_device.createPrivateResource(m_fixedData);
		if (FAILED(result))
		{
			throw HResultException(result);
		}
		m_handle = m_fixedData.hResource;

		m_isPaletteResolvedSurfaceUpToDate.resize(m_fixedData.SurfCount);
		m_isColorKeyedSurfaceUpToDate.resize(m_fixedData.SurfCount);

		if (D3DDDIPOOL_SYSTEMMEM != m_fixedData.Pool && m_origData.Flags.ZBuffer &&
			!m_device.getAdapter().getInfo().isD3D9On12)
		{
			m_lockData.resize(m_origData.SurfCount);
			for (UINT i = 0; i < m_origData.SurfCount; ++i)
			{
				m_lockData[i].isSysMemUpToDate = true;
				m_lockData[i].isVidMemUpToDate = true;
				m_lockData[i].isMsaaUpToDate = m_msaaSurface.resource;
				m_lockData[i].isMsaaResolvedUpToDate = m_msaaResolvedSurface.resource;
				m_lockData[i].qpcLastCpuAccess = Time::queryPerformanceCounter();
			}
		}
		else if (D3DDDIPOOL_SYSTEMMEM == m_fixedData.Pool && 0 != m_formatInfo.bytesPerPixel)
		{
			m_lockData.resize(m_origData.SurfCount);
			for (UINT i = 0; i < m_origData.SurfCount; ++i)
			{
				m_lockData[i].data = const_cast<void*>(m_origData.pSurfList[i].pSysMem);
				m_lockData[i].pitch = m_origData.pSurfList[i].SysMemPitch;
				m_lockData[i].isSysMemUpToDate = true;
			}
		}
		else
		{
			createLockResource();
		}

		if (isPalettizedTexture())
		{
			m_device.getRepo().getSurface(m_paletteResolvedSurface,
				m_fixedData.pSurfList[0].Width, m_fixedData.pSurfList[0].Height,
				D3DDDIFMT_X8R8G8B8, DDSCAPS_TEXTURE | DDSCAPS_3DDEVICE | DDSCAPS_VIDEOMEMORY |
				(m_fixedData.MipLevels > 1 ? DDSCAPS_MIPMAP : 0),
				m_fixedData.SurfCount,
				m_fixedData.Flags.CubeMap ? DDSCAPS2_CUBEMAP : 0);
		}

		if (m_fixedData.Flags.ZBuffer && !m_device.getAdapter().getInfo().isD3D9On12)
		{
			m_device.getRepo().getSurface(m_nullRt, m_fixedData.pSurfList[0].Width, m_fixedData.pSurfList[0].Height,
				FOURCC_NULL, DDSCAPS_3DDEVICE | DDSCAPS_VIDEOMEMORY, m_fixedData.SurfCount);
		}

		data.hResource = m_fixedData.hResource;
		updateConfig();
	}

	Resource::~Resource()
	{
		if (m_msaaSurface.surface || m_msaaResolvedSurface.surface || m_lockRefSurface.surface)
		{
			auto& repo = m_device.getRepo();
			repo.release(m_msaaSurface);
			repo.release(m_msaaResolvedSurface);
			repo.release(m_lockRefSurface);
		}
	}

	HRESULT Resource::blt(D3DDDIARG_BLT data)
	{
		if (!isValidRect(data.DstSubResourceIndex, data.DstRect))
		{
			return S_OK;
		}

		if (m_lockResource)
		{
			m_isPaletteResolvedSurfaceUpToDate[data.DstSubResourceIndex] = false;
			m_isColorKeyedSurfaceUpToDate[data.DstSubResourceIndex] = false;
		}

		auto srcResource = m_device.getResource(data.hSrcResource);
		if (!srcResource)
		{
			prepareForBltDst(data);
			return m_device.getOrigVtable().pfnBlt(m_device, &data);
		}

		if (!srcResource->isValidRect(data.SrcSubResourceIndex, data.SrcRect))
		{
			return S_OK;
		}

		if (D3DDDIPOOL_SYSTEMMEM == m_fixedData.Pool &&
			D3DDDIPOOL_SYSTEMMEM == srcResource->m_fixedData.Pool)
		{
			return m_device.getOrigVtable().pfnBlt(m_device, &data);
		}

		if (srcResource->m_fixedData.Flags.ZBuffer && srcResource->m_msaaSurface.resource &&
			!m_device.getAdapter().getInfo().isMsaaDepthResolveSupported)
		{
			logUnsupportedMsaaDepthBufferResolve();
			return S_OK;
		}

		auto steamResources = Overlay::Steam::getResources();
		if (steamResources.rtResource == srcResource && steamResources.bbResource)
		{
			srcResource = steamResources.bbResource;
			data.hSrcResource = *steamResources.bbResource;
			data.SrcSubResourceIndex = steamResources.bbSubResourceIndex;
		}
		else if (shouldBltViaCpu(data, *srcResource))
		{
			return bltViaCpu(data, *srcResource);
		}

		return bltViaGpu(data, *srcResource);
	}

	HRESULT Resource::bltLock(D3DDDIARG_LOCK& data)
	{
		LOG_FUNC("Resource::bltLock", data);

		if (m_lockResource)
		{
			if (data.Flags.ReadOnly)
			{
				prepareForCpuRead(data.SubResourceIndex);
			}
			else
			{
				prepareForCpuWrite(data.SubResourceIndex);
			}
		}

		auto& lockData = m_lockData[data.SubResourceIndex];
		unsigned char* ptr = static_cast<unsigned char*>(lockData.data);
		if (data.Flags.AreaValid)
		{
			ptr += data.Area.top * lockData.pitch + data.Area.left * m_formatInfo.bytesPerPixel;
		}

		data.pSurfData = ptr;
		data.Pitch = lockData.pitch;
		return LOG_RESULT(S_OK);
	}

	HRESULT Resource::bltViaCpu(D3DDDIARG_BLT data, Resource& srcResource)
	{
		D3DDDIARG_LOCK srcLock = {};
		srcLock.hResource = data.hSrcResource;
		srcLock.SubResourceIndex = data.SrcSubResourceIndex;
		if (D3DDDIPOOL_SYSTEMMEM == srcResource.m_fixedData.Pool)
		{
			srcLock.Flags.NotifyOnly = 1;
		}
		else
		{
			srcLock.Area = data.SrcRect;
			srcLock.Flags.AreaValid = 1;
			srcLock.Flags.ReadOnly = 1;
		}

		HRESULT result = srcResource.lock(srcLock);
		if (FAILED(result))
		{
			return result;
		}

		D3DDDIARG_LOCK dstLock = {};
		dstLock.hResource = data.hDstResource;
		dstLock.SubResourceIndex = data.DstSubResourceIndex;
		if (D3DDDIPOOL_SYSTEMMEM == m_fixedData.Pool)
		{
			dstLock.Flags.NotifyOnly = 1;
		}
		else
		{
			dstLock.Area = data.DstRect;
			dstLock.Flags.AreaValid = 1;
		}

		result = lock(dstLock);
		if (SUCCEEDED(result))
		{
			if (D3DDDIPOOL_SYSTEMMEM == srcResource.m_fixedData.Pool)
			{
				auto& lockData = srcResource.m_lockData[data.SrcSubResourceIndex];
				srcLock.pSurfData = static_cast<BYTE*>(lockData.data) + data.SrcRect.top * lockData.pitch +
					data.SrcRect.left * m_formatInfo.bytesPerPixel;
				srcLock.Pitch = lockData.pitch;
			}

			if (D3DDDIPOOL_SYSTEMMEM == m_fixedData.Pool)
			{
				auto& lockData = m_lockData[data.DstSubResourceIndex];
				dstLock.pSurfData = static_cast<BYTE*>(lockData.data) + data.DstRect.top * lockData.pitch +
					data.DstRect.left * m_formatInfo.bytesPerPixel;
				dstLock.Pitch = lockData.pitch;
			}

			DDraw::Blitter::blt(
				dstLock.pSurfData,
				dstLock.Pitch,
				data.DstRect.right - data.DstRect.left,
				data.DstRect.bottom - data.DstRect.top,
				srcLock.pSurfData,
				srcLock.Pitch,
				(1 - 2 * data.Flags.MirrorLeftRight) * (data.SrcRect.right - data.SrcRect.left),
				(1 - 2 * data.Flags.MirrorUpDown) * (data.SrcRect.bottom - data.SrcRect.top),
				m_formatInfo.bytesPerPixel,
				data.Flags.DstColorKey ? reinterpret_cast<const DWORD*>(&data.ColorKey) : nullptr,
				data.Flags.SrcColorKey ? reinterpret_cast<const DWORD*>(&data.ColorKey) : nullptr);

			D3DDDIARG_UNLOCK dstUnlock = {};
			dstUnlock.hResource = dstLock.hResource;
			dstUnlock.SubResourceIndex = dstLock.SubResourceIndex;
			dstUnlock.Flags.NotifyOnly = dstLock.Flags.NotifyOnly;
			unlock(dstUnlock);
		}

		D3DDDIARG_UNLOCK srcUnlock = {};
		srcUnlock.hResource = srcLock.hResource;
		srcUnlock.SubResourceIndex = srcLock.SubResourceIndex;
		srcUnlock.Flags.NotifyOnly = srcLock.Flags.NotifyOnly;
		srcResource.unlock(srcUnlock);
		return result;
	}

	HRESULT Resource::bltViaGpu(D3DDDIARG_BLT data, Resource& srcResource)
	{
		if (srcResource.m_lockResource)
		{
			srcResource.loadFromLockRefResource(data.SrcSubResourceIndex);
		}

		if (m_isPrimary && !m_isPrimaryScalingNeeded && srcResource.isScaled(data.SrcSubResourceIndex))
		{
			m_isPrimaryScalingNeeded = true;
			updateConfig();
		}

		Resource* srcRes = &srcResource;
		if (m_msaaResolvedSurface.resource && srcResource.m_msaaResolvedSurface.resource &&
			(srcResource.m_lockData[data.SrcSubResourceIndex].isMsaaResolvedUpToDate ||
				srcResource.m_lockData[data.SrcSubResourceIndex].isMsaaUpToDate))
		{
			srcResource.loadMsaaResolvedResource(data.SrcSubResourceIndex);
			srcRes = srcResource.m_msaaResolvedSurface.resource;
			data.hSrcResource = *srcRes;
			srcResource.scaleRect(data.SrcRect);
			if (!m_lockData[data.DstSubResourceIndex].isMsaaUpToDate)
			{
				loadMsaaResolvedResource(data.DstSubResourceIndex);
			}
		}
		else
		{
			srcResource.prepareForBltSrc(data);
		}

		if (!m_fixedData.Flags.ZBuffer &&
			(0 == m_formatInfo.bytesPerPixel || 0 == srcResource.m_formatInfo.bytesPerPixel))
		{
			if (m_lockResource)
			{
				loadVidMemResource(data.DstSubResourceIndex);
				clearUpToDateFlags(data.DstSubResourceIndex);
				m_lockData[data.DstSubResourceIndex].isVidMemUpToDate = true;
			}
			return m_device.getOrigVtable().pfnBlt(m_device, &data);
		}

		Resource& dstRes = prepareForBltDst(data);
		return shaderBlt(data, dstRes, *srcRes,
			Config::Settings::BltFilter::BILINEAR == Config::bltFilter.get() ? D3DTEXF_LINEAR : D3DTEXF_POINT);
	}

	bool Resource::canCopySubResource(const D3DDDIARG_BLT& data, Resource& srcResource)
	{
		if (data.Flags.SrcColorKey ||
			data.Flags.MirrorLeftRight ||
			data.Flags.MirrorUpDown ||
			D3DDDIMULTISAMPLE_NONE != m_fixedData.MultisampleType)
		{
			return false;
		}

		if (m_fixedData.Flags.ZBuffer)
		{
			return m_fixedData.Format == srcResource.m_fixedData.Format &&
				(!m_device.getAdapter().getInfo().isD3D9On12 || Rect::isEqualSize(data.SrcRect, data.DstRect));
		}

		return (m_fixedData.Format == srcResource.m_fixedData.Format ||
			D3DDDIPOOL_SYSTEMMEM != srcResource.m_fixedData.Pool && D3DDDIPOOL_SYSTEMMEM != m_fixedData.Pool) &&
			Rect::isEqualSize(data.SrcRect, data.DstRect);
	}

	void Resource::clearRectExterior(UINT subResourceIndex, const RECT& rect)
	{
		const LONG width = m_fixedData.pSurfList[subResourceIndex].Width;
		const LONG height = m_fixedData.pSurfList[subResourceIndex].Height;
		if (rect.left > 0)
		{
			clearRectInterior(subResourceIndex, { 0, 0, rect.left, height });
		}
		if (rect.right < width)
		{
			clearRectInterior(subResourceIndex, { rect.right, 0, width, height });
		}
		if (rect.top > 0)
		{
			clearRectInterior(subResourceIndex, { rect.left, 0, rect.right, rect.top });
		}
		if (rect.bottom < height)
		{
			clearRectInterior(subResourceIndex, { rect.left, rect.bottom, rect.right, height });
		}
	}

	void Resource::clearRectInterior(UINT subResourceIndex, const RECT& rect)
	{
		D3DDDIARG_COLORFILL data = {};
		data.hResource = m_handle;
		data.SubResourceIndex = subResourceIndex;
		data.DstRect = rect;
		m_device.getOrigVtable().pfnColorFill(m_device, &data);
	}

	void Resource::clearUpToDateFlags(UINT subResourceIndex)
	{
		m_lockData[subResourceIndex].isMsaaUpToDate = false;
		m_lockData[subResourceIndex].isMsaaResolvedUpToDate = false;
		m_lockData[subResourceIndex].isVidMemUpToDate = false;
		m_lockData[subResourceIndex].isSysMemUpToDate = false;
	}

	void Resource::clipRect(UINT subResourceIndex, RECT& rect)
	{
		rect.left = std::max<LONG>(rect.left, 0);
		rect.top = std::max<LONG>(rect.top, 0);
		rect.right = std::min<LONG>(rect.right, m_fixedData.pSurfList[subResourceIndex].Width);
		rect.bottom = std::min<LONG>(rect.bottom, m_fixedData.pSurfList[subResourceIndex].Height);
	}

	HRESULT Resource::colorFill(D3DDDIARG_COLORFILL data)
	{
		LOG_FUNC("Resource::colorFill", data);
		clipRect(data.SubResourceIndex, data.DstRect);
		if (data.DstRect.left >= data.DstRect.right || data.DstRect.top >= data.DstRect.bottom)
		{
			return S_OK;
		}

		if (m_lockResource)
		{
			m_isPaletteResolvedSurfaceUpToDate[data.SubResourceIndex] = false;
			m_isColorKeyedSurfaceUpToDate[data.SubResourceIndex] = false;

			auto& lockData = m_lockData[data.SubResourceIndex];
			if (lockData.isSysMemUpToDate && !lockData.isVidMemUpToDate)
			{
				auto dstBuf = static_cast<BYTE*>(lockData.data) +
					data.DstRect.top * lockData.pitch + data.DstRect.left * m_formatInfo.bytesPerPixel;

				DDraw::Blitter::colorFill(dstBuf, lockData.pitch,
					data.DstRect.right - data.DstRect.left, data.DstRect.bottom - data.DstRect.top,
					m_formatInfo.bytesPerPixel, convertFrom32Bit(m_formatInfo, data.Color));

				return LOG_RESULT(S_OK);
			}
		}

		if (D3DDDIFMT_P8 == m_fixedData.Format)
		{
			data.Color |= data.Color << 16;
		}

		prepareForBltDst(data.hResource, data.SubResourceIndex, data.DstRect);
		return LOG_RESULT(m_device.getOrigVtable().pfnColorFill(m_device, &data));
	}

	HRESULT Resource::copySubResource(Resource& dstResource, Resource& srcResource, UINT subResourceIndex)
	{
		return copySubResourceRegion(dstResource, subResourceIndex, dstResource.getRect(subResourceIndex),
			srcResource, subResourceIndex, srcResource.getRect(subResourceIndex));
	}

	HRESULT Resource::copySubResource(HANDLE dstResource, HANDLE srcResource, UINT subResourceIndex)
	{
		return copySubResourceRegion(dstResource, subResourceIndex, getRect(subResourceIndex),
			srcResource, subResourceIndex, getRect(subResourceIndex));
	}

	HRESULT Resource::copySubResourceRegion(UINT dstIndex, const RECT& dstRect,
		HANDLE src, UINT srcIndex, const RECT& srcRect)
	{
		return copySubResourceRegion(*this, dstIndex, dstRect, src, srcIndex, srcRect);
	}

	HRESULT Resource::copySubResourceRegion(HANDLE dst, UINT dstIndex, const RECT& dstRect,
		HANDLE src, UINT srcIndex, const RECT& srcRect)
	{
		LOG_FUNC("Resource::copySubResourceRegion", dst, dstIndex, dstRect, src, srcIndex, srcRect);
		D3DDDIARG_BLT data = {};
		data.hDstResource = dst;
		data.DstSubResourceIndex = dstIndex;
		data.DstRect = dstRect;
		data.hSrcResource = src;
		data.SrcSubResourceIndex = srcIndex;
		data.SrcRect = srcRect;
		data.Flags.Point = 1;

		HRESULT result = LOG_RESULT(m_device.getOrigVtable().pfnBlt(m_device, &data));
		if (FAILED(result))
		{
			LOG_ONCE("ERROR: Resource::copySubResourceRegion failed: " << Compat::hex(result));
		}
		return result;
	}

	void Resource::createGdiLockResource()
	{
		LOG_FUNC("Resource::createGdiLockResource");
		auto gdiSurfaceDesc(Gdi::VirtualScreen::getSurfaceDesc(DDraw::PrimarySurface::getMonitorInfo().rcEmulated));
		if (!gdiSurfaceDesc.lpSurface)
		{
			return;
		}

		D3DDDI_SURFACEINFO surfaceInfo = {};
		surfaceInfo.Width = gdiSurfaceDesc.dwWidth;
		surfaceInfo.Height = gdiSurfaceDesc.dwHeight;
		surfaceInfo.pSysMem = gdiSurfaceDesc.lpSurface;
		surfaceInfo.SysMemPitch = gdiSurfaceDesc.lPitch;

		m_lockData.resize(m_fixedData.SurfCount);
		createSysMemResource({ surfaceInfo });
		if (m_lockResource)
		{
			clearUpToDateFlags(0);
			m_lockData[0].isSysMemUpToDate = true;
		}
		else
		{
			m_lockData.clear();
		}
	}

	void Resource::createLockResource()
	{
		D3DDDI_RESOURCEFLAGS flags = {};
		flags.Value = g_resourceTypeFlags;
		flags.RenderTarget = 0;
		flags.Texture = 0;
		if (m_device.getAdapter().getInfo().isD3D9On12)
		{
			flags.ZBuffer = 0;
		}
		if (D3DDDIPOOL_SYSTEMMEM == m_fixedData.Pool ||
			m_isSurfaceRepoResource && !SurfaceRepository::isLockResourceEnabled() ||
			0 == m_formatInfo.bytesPerPixel ||
			0 != (m_fixedData.Flags.Value & flags.Value) ||
			m_fixedData.Flags.Texture && (DDraw::Surface::getCurrentSurfaceCaps().dwCaps2 & 0x100000))   // managed texture
		{
			return;
		}

		const auto ALIGNMENT = DDraw::Surface::ALIGNMENT;
		std::vector<D3DDDI_SURFACEINFO> surfaceInfo(m_fixedData.SurfCount);
		for (UINT i = 0; i < m_fixedData.SurfCount; ++i)
		{
			surfaceInfo[i].Width = m_fixedData.pSurfList[i].Width;
			surfaceInfo[i].Height = m_fixedData.pSurfList[i].Height;
			surfaceInfo[i].SysMemPitch = (surfaceInfo[i].Width * m_formatInfo.bytesPerPixel + 3) & ~3;
			if (i != 0)
			{
				std::uintptr_t offset = reinterpret_cast<std::uintptr_t>(surfaceInfo[i - 1].pSysMem) +
					((surfaceInfo[i - 1].SysMemPitch * surfaceInfo[i - 1].Height + ALIGNMENT - 1) / ALIGNMENT * ALIGNMENT);
				surfaceInfo[i].pSysMem = reinterpret_cast<void*>(offset);
			}
		}

		unsigned topRows = 0;
		unsigned extraRows = 0;
		if (1 == m_fixedData.SurfCount && !m_origData.Flags.Texture)
		{
			topRows = Config::surfacePatches.getTopRows(surfaceInfo.back().Height);
			extraRows = Config::surfacePatches.getExtraRows(surfaceInfo.back().Height);
		}

		std::uintptr_t bufferSize = reinterpret_cast<std::uintptr_t>(surfaceInfo.back().pSysMem) +
			surfaceInfo.back().SysMemPitch * (surfaceInfo.back().Height + extraRows) + ALIGNMENT;
		m_lockBuffer.reset(HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, bufferSize));

		BYTE* bufferStart = static_cast<BYTE*>(DDraw::Surface::alignBuffer(
			static_cast<BYTE*>(m_lockBuffer.get()) + topRows * surfaceInfo.back().SysMemPitch));
		for (UINT i = 0; i < m_fixedData.SurfCount; ++i)
		{
			surfaceInfo[i].pSysMem = bufferStart + reinterpret_cast<uintptr_t>(surfaceInfo[i].pSysMem);
		}

		createSysMemResource(surfaceInfo);
		if (!m_lockResource)
		{
			m_lockBuffer.reset();
			m_lockData.clear();
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

		HRESULT result = m_device.createPrivateResource(data);
		if (SUCCEEDED(result))
		{
			m_lockResource.reset(data.hResource);
			m_lockData.resize(surfaceInfo.size());
			for (std::size_t i = 0; i < surfaceInfo.size(); ++i)
			{
				m_lockData[i].data = const_cast<void*>(surfaceInfo[i].pSysMem);
				m_lockData[i].pitch = surfaceInfo[i].SysMemPitch;
				m_lockData[i].isSysMemUpToDate = true;
				m_lockData[i].isVidMemUpToDate = true;
				m_lockData[i].isMsaaUpToDate = m_msaaSurface.resource;
				m_lockData[i].isMsaaResolvedUpToDate = m_msaaResolvedSurface.resource;
				m_lockData[i].isRefLocked = false;
				m_lockData[i].qpcLastCpuAccess = Time::queryPerformanceCounter();
			}
		}

		LOG_RESULT(m_lockResource.get());
	}

	HRESULT Resource::depthFill(const D3DDDIARG_DEPTHFILL& data)
	{
		auto& dstResource = prepareForGpuWrite(data.SubResourceIndex);
		auto& si = dstResource.getFixedDesc().pSurfList[data.SubResourceIndex];

		auto& state = m_device.getState();
		state.flush();
		state.setTempRenderTarget({ 0, dstResource.getNullRtHandle(), data.SubResourceIndex});
		state.setTempDepthStencil({ dstResource });
		state.setTempRenderState({ D3DDDIRS_SCISSORTESTENABLE, FALSE });
		state.setTempViewport({ 0, 0, si.Width, si.Height });

		RECT rect = data.DstRect;
		scaleRect(rect);

		D3DDDIARG_CLEAR clear = {};
		clear.Flags = D3DCLEAR_ZBUFFER;
		clear.FillDepth = getComponentAsFloat(data.Depth, m_formatInfo.depth);
		if (0 != m_formatInfo.stencil.bitCount && 0 != dstResource.m_formatInfo.stencil.bitCount)
		{
			clear.Flags |= D3DCLEAR_STENCIL;
			clear.FillStencil = getComponent(data.Depth, m_formatInfo.stencil);
		}
		return m_device.getOrigVtable().pfnClear(m_device, &clear, 1, &rect);
	}

	void Resource::disableClamp()
	{
		m_isClampable = false;
	}

	void Resource::enableConfig(bool enable)
	{
		g_enableConfig = enable;
	}

	void Resource::fixResourceData()
	{
		if (m_fixedData.Flags.MatchGdiPrimary)
		{
			auto& mi = DDraw::PrimarySurface::getMonitorInfo();
			RECT r = DDraw::RealPrimarySurface::isExclusiveFullscreen() ? mi.rcReal : mi.rcDpiAware;
			if (!IsRectEmpty(&r))
			{
				for (auto& surface : m_fixedData.surfaceData)
				{
					surface.Width = r.right - r.left;
					surface.Height = r.bottom - r.top;
				}
			}
			m_fixedData.Format = D3DDDIFMT_X8R8G8B8;
		}
		else if (D3DDDIFMT_UNKNOWN != g_formatOverride)
		{
			m_fixedData.Format = g_formatOverride;
		}
		else if (D3DDDIFMT_L8 == m_fixedData.Format && !m_fixedData.Flags.Texture)
		{
			m_fixedData.Flags.Texture = 1;
			m_fixedData.MipLevels = 1;
		}
		else if (m_fixedData.Flags.RenderTarget && m_device.getAdapter().isEmulatedRenderTargetFormat(m_fixedData.Format))
		{
			m_fixedData.Flags.RenderTarget = 0;
		}

		const auto& formatOps = m_device.getAdapter().getInfo().fixedFormatOps;
		switch (m_fixedData.Format)
		{
		case D3DDDIFMT_D16:
			if (formatOps.find(FOURCC_DF16) != formatOps.end())
			{
				m_fixedData.Format = FOURCC_DF16;
			}
			break;

		case D3DDDIFMT_S8D24:
		case D3DDDIFMT_D24S8:
		case D3DDDIFMT_X8D24:
		case D3DDDIFMT_D24X8:
			if (formatOps.find(FOURCC_INTZ) != formatOps.end())
			{
				m_fixedData.Format = FOURCC_INTZ;
			}
			break;
		}

		if (D3DDDIMULTISAMPLE_NONE != g_msaaOverride.first)
		{
			m_fixedData.MultisampleType = g_msaaOverride.first;
			m_fixedData.MultisampleQuality = g_msaaOverride.second;
		}
		else if (!m_fixedData.Flags.Texture &&
			!m_fixedData.Flags.MatchGdiPrimary &&
			D3DDDIPOOL_SYSTEMMEM != m_fixedData.Pool &&
			(m_origData.Flags.RenderTarget || m_fixedData.Flags.ZBuffer))
		{
			auto it = formatOps.find(m_fixedData.Format);
			if (it != formatOps.end() && (it->second.Operations & FORMATOP_TEXTURE))
			{
				m_fixedData.Flags.Texture = 1;
				m_fixedData.MipLevels = 1;
			}
		}

		if (D3DDDIPOOL_SYSTEMMEM == m_fixedData.Pool)
		{
			switch (m_fixedData.Format)
			{
			case FOURCC_DXT1:
			case FOURCC_DXT2:
			case FOURCC_DXT3:
			case FOURCC_DXT4:
			case FOURCC_DXT5:
				for (unsigned i = 0; i < m_fixedData.SurfCount; ++i)
				{
					m_fixedData.surfaceData[i].SysMemPitch /= (m_fixedData.surfaceData[i].Height + 3) / 4;
				}
				break;
			}
		}

		if (D3DDDIPOOL_SYSTEMMEM == m_fixedData.Pool &&
			1 == m_fixedData.SurfCount &&
			0 == m_fixedData.pSurfList[0].Depth &&
			0 != D3dDdi::getFormatInfo(m_fixedData.Format).bytesPerPixel)
		{
			const auto& caps = m_device.getAdapter().getInfo().d3dExtendedCaps;
			auto& surfaceInfo = m_fixedData.surfaceData[0];
			if (surfaceInfo.Width > caps.dwMaxTextureWidth)
			{
				surfaceInfo.Width = caps.dwMaxTextureWidth;
				m_isOversized = true;
			}
			if (surfaceInfo.Height > caps.dwMaxTextureHeight)
			{
				surfaceInfo.Height = caps.dwMaxTextureHeight;
				m_isOversized = true;
			}
		}
	}

	D3DDDIFORMAT Resource::getFormatConfig()
	{
		if (m_origData.Flags.RenderTarget && !m_fixedData.Flags.RenderTarget)
		{
			return 0 != m_formatInfo.alpha.bitCount ? D3DDDIFMT_A8R8G8B8 : D3DDDIFMT_X8R8G8B8;
		}

		if (D3DDDIFMT_X8R8G8B8 == m_fixedData.Format || D3DDDIFMT_R5G6B5 == m_fixedData.Format)
		{
			if (!m_origData.Flags.RenderTarget || !m_origData.Flags.Texture)
			{
				return m_device.getAdapter().getRenderColorDepthSrcFormat(m_fixedData.Format);
			}
		}
		else if (m_fixedData.Flags.ZBuffer && Config::Settings::DepthFormat::APP != Config::depthFormat.get() &&
			getFormatInfo(m_fixedData.Format).depth.bitCount != Config::depthFormat.get())
		{
			auto& formatOps = m_device.getAdapter().getInfo().fixedFormatOps;
			switch (Config::depthFormat.get())
			{
#define USE_FORMAT(format) if (formatOps.find(format) != formatOps.end()) return format
			case 32:
				USE_FORMAT(D3DDDIFMT_D32);
				USE_FORMAT(D3DDDIFMT_D32_LOCKABLE);
				USE_FORMAT(D3DDDIFMT_D32F_LOCKABLE);
			case 24:
				USE_FORMAT(D3DDDIFMT_S8D24);
				USE_FORMAT(D3DDDIFMT_D24S8);
				USE_FORMAT(D3DDDIFMT_X8D24);
				USE_FORMAT(D3DDDIFMT_D24X8);
			case 16:
				USE_FORMAT(D3DDDIFMT_D16);
#undef USE_FORMAT
			}
		}
		return m_fixedData.Format;
	}

	void* Resource::getLockPtr(UINT subResourceIndex)
	{
		return m_lockData.empty() ? nullptr : m_lockData[subResourceIndex].data;
	}

	UINT Resource::getMappedColorKey(UINT colorKey) const
	{
		if (isPalettizedTexture())
		{
			return reinterpret_cast<UINT&>(m_device.getPalette(m_paletteHandle)[colorKey]) & 0xFFFFFF;
		}
		return colorKey;
	}

	std::pair<D3DDDIMULTISAMPLE_TYPE, UINT> Resource::getMultisampleConfig()
	{
		if (!m_isPrimary || m_origData.Flags.RenderTarget)
		{
			return m_device.getAdapter().getMultisampleConfig(m_fixedData.Format);
		}
		return { D3DDDIMULTISAMPLE_NONE, 0 };
	}

	RECT Resource::getRect(UINT subResourceIndex) const
	{
		const auto& si = m_fixedData.pSurfList[subResourceIndex];
		return { 0, 0, static_cast<LONG>(si.Width), static_cast<LONG>(si.Height) };
	}

	SIZE Resource::getScaledSize()
	{
		SIZE size = { static_cast<LONG>(m_fixedData.pSurfList[0].Width), static_cast<LONG>(m_fixedData.pSurfList[0].Height) };
		if (m_isPrimary && m_isPrimaryScalingNeeded || m_origData.Flags.RenderTarget || m_fixedData.Flags.ZBuffer)
		{
			return m_device.getAdapter().getScaledSize(size);
		}
		return size;
	}

	void Resource::invalidatePalettizedTexture()
	{
		m_isPaletteResolvedSurfaceUpToDate.assign(m_fixedData.SurfCount, false);
		m_isColorKeyedSurfaceUpToDate.assign(m_fixedData.SurfCount, false);
	}

	bool Resource::isPalettizedTexture() const
	{
		return D3DDDIFMT_P8 == m_origData.Format && m_origData.Flags.Texture && D3DDDIPOOL_SYSTEMMEM != m_origData.Pool;
	}

	bool Resource::isScaled(UINT subResourceIndex)
	{
		if (!m_msaaResolvedSurface.resource)
		{
			return false;
		}

		auto& si = m_fixedData.pSurfList[subResourceIndex];
		auto& scaledSi = m_msaaResolvedSurface.resource->getFixedDesc().pSurfList[subResourceIndex];
		return si.Width != scaledSi.Width || si.Height != scaledSi.Height;
	}

	bool Resource::isValidRect(UINT subResourceIndex, const RECT& rect)
	{
		return rect.left >= 0 && rect.top >= 0 && rect.left < rect.right && rect.top < rect.bottom &&
			rect.right <= static_cast<LONG>(m_fixedData.pSurfList[subResourceIndex].Width) &&
			rect.bottom <= static_cast<LONG>(m_fixedData.pSurfList[subResourceIndex].Height);
	}

	void Resource::loadFromLockRefResource(UINT subResourceIndex)
	{
		if (m_lockData[subResourceIndex].isRefLocked)
		{
			m_lockData[subResourceIndex].isRefLocked = false;
			loadVidMemResource(subResourceIndex);

			auto srcResource = this;
			auto srcIndex = subResourceIndex;
			auto& si = m_fixedData.pSurfList[subResourceIndex];
			const RECT srcRect = { 0, 0, static_cast<LONG>(si.Width), static_cast<LONG>(si.Height) };
			if (!m_fixedData.Flags.Texture)
			{
				auto& texture = m_device.getRepo().getTempTexture(si.Width, si.Height, m_fixedData.Format);
				if (!texture.resource)
				{
					return;
				}
				srcResource = texture.resource;
				srcIndex = 0;
				copySubResourceRegion(*srcResource, 0, srcRect, m_handle, subResourceIndex, srcRect);
			}

			const RECT dstRect = { 0, 0, static_cast<LONG>(m_msaaResolvedSurface.width),
				static_cast<LONG>(m_msaaResolvedSurface.height) };
			m_device.getShaderBlitter().lockRefBlt(*m_msaaResolvedSurface.resource, subResourceIndex, dstRect,
				*srcResource, srcIndex, srcRect, *m_lockRefSurface.resource);
			m_lockData[subResourceIndex].isMsaaResolvedUpToDate = true;
		}
	}

	void Resource::loadMsaaResource(UINT subResourceIndex)
	{
		if (!m_lockData[subResourceIndex].isMsaaUpToDate)
		{
			if (m_msaaResolvedSurface.resource)
			{
				loadMsaaResolvedResource(subResourceIndex);
				if (m_fixedData.Flags.ZBuffer)
				{
					const bool isD3D9On12 = m_device.getAdapter().getInfo().isD3D9On12;
					if (isD3D9On12 ||
						m_nullRt.resource && isDepthTexture(m_msaaResolvedSurface.resource->m_fixedData.Format))
					{
						RECT r = m_msaaResolvedSurface.resource->getRect(0);
						m_device.getShaderBlitter().depthBlt(
							*m_msaaSurface.resource, r, *m_msaaResolvedSurface.resource, r);
					}
				}
				else
				{
					copySubResource(*m_msaaSurface.resource, *m_msaaResolvedSurface.resource, subResourceIndex);
				}
			}
			else
			{
				loadVidMemResource(subResourceIndex);
				copySubResource(*m_msaaSurface.resource, *this, subResourceIndex);
			}
			m_lockData[subResourceIndex].isMsaaUpToDate = true;
		}
	}

	void Resource::loadMsaaResolvedResource(UINT subResourceIndex)
	{
		loadFromLockRefResource(subResourceIndex);
		if (m_lockData[subResourceIndex].isMsaaResolvedUpToDate)
		{
			return;
		}

		if (m_lockData[subResourceIndex].isMsaaUpToDate)
		{
			if (m_fixedData.Flags.ZBuffer)
			{
				if (m_device.getAdapter().getInfo().isMsaaDepthResolveSupported)
				{
					resolveMsaaDepthBuffer();
				}
				else
				{
					logUnsupportedMsaaDepthBufferResolve();
				}
			}
			else
			{
				copySubResource(*m_msaaResolvedSurface.resource, *m_msaaSurface.resource, subResourceIndex);
			}
		}
		else
		{
			loadVidMemResource(subResourceIndex);
			D3DDDIARG_BLT blt = {};
			blt.hSrcResource = *this;
			blt.SrcSubResourceIndex = subResourceIndex;
			blt.SrcRect = getRect(subResourceIndex);
			blt.hDstResource = *m_msaaResolvedSurface.resource;
			blt.DstSubResourceIndex = subResourceIndex;
			blt.DstRect = m_msaaResolvedSurface.resource->getRect(subResourceIndex);
			shaderBlt(blt, *m_msaaResolvedSurface.resource, *this, D3DTEXF_POINT);
		}
		m_lockData[subResourceIndex].isMsaaResolvedUpToDate = true;
	}

	void Resource::loadSysMemResource(UINT subResourceIndex)
	{
		if (!m_lockData[subResourceIndex].isSysMemUpToDate)
		{
			loadVidMemResource(subResourceIndex);
			copySubResource(m_lockResource.get(), *this, subResourceIndex);
			notifyLock(subResourceIndex);
			m_lockData[subResourceIndex].isSysMemUpToDate = true;
		}
		m_lockData[subResourceIndex].qpcLastCpuAccess = Time::queryPerformanceCounter();
	}

	void Resource::loadVidMemResource(UINT subResourceIndex)
	{
		if (m_lockData[subResourceIndex].isVidMemUpToDate)
		{
			return;
		}

		if (m_lockData[subResourceIndex].isMsaaUpToDate || m_lockData[subResourceIndex].isMsaaResolvedUpToDate)
		{
			loadMsaaResolvedResource(subResourceIndex);

			D3DDDIARG_BLT blt = {};
			blt.hSrcResource = *m_msaaResolvedSurface.resource;
			blt.SrcSubResourceIndex = subResourceIndex;
			blt.SrcRect = m_msaaResolvedSurface.resource->getRect(subResourceIndex);
			blt.hDstResource = *this;
			blt.DstSubResourceIndex = subResourceIndex;
			blt.DstRect = getRect(subResourceIndex);
			shaderBlt(blt, *this, *m_msaaResolvedSurface.resource,
				Config::Settings::ResolutionScaleFilter::BILINEAR == Config::resolutionScaleFilter.get()
				? D3DTEXF_LINEAR | D3DTEXF_SRGB
				: D3DTEXF_POINT);
		}
		else
		{
			copySubResource(*this, m_lockResource.get(), subResourceIndex);
			notifyLock(subResourceIndex);
			m_lockData[subResourceIndex].isRefLocked = false;
		}
		m_lockData[subResourceIndex].isVidMemUpToDate = true;
	}

	HRESULT Resource::lock(D3DDDIARG_LOCK& data)
	{
		if (D3DDDIMULTISAMPLE_NONE != m_fixedData.MultisampleType || FOURCC_NULL == m_fixedData.Format)
		{
			return E_FAIL;
		}

		if (g_readOnlyLock)
		{
			data.Flags.ReadOnly = true;
		}

		if (m_lockResource || m_isOversized)
		{
			return bltLock(data);
		}

		if (!data.Flags.ReadOnly)
		{
			m_isPaletteResolvedSurfaceUpToDate[data.SubResourceIndex] = false;
			m_isColorKeyedSurfaceUpToDate[data.SubResourceIndex] = false;
		}

		if (m_fixedData.Flags.ZBuffer && m_msaaResolvedSurface.resource)
		{
			loadVidMemResource(0);
			if (!data.Flags.ReadOnly)
			{
				m_lockData[0].isMsaaUpToDate = false;
				m_lockData[0].isMsaaResolvedUpToDate = false;
			}
		}
		return m_device.getOrigVtable().pfnLock(m_device, &data);
	}

	void Resource::notifyLock(UINT subResourceIndex)
	{
		D3DDDIARG_LOCK lock = {};
		lock.hResource = D3DDDIPOOL_SYSTEMMEM == m_fixedData.Pool ? m_handle : m_lockResource.get();
		lock.SubResourceIndex = subResourceIndex;
		lock.Flags.NotifyOnly = 1;
		m_device.getOrigVtable().pfnLock(m_device, &lock);

		D3DDDIARG_UNLOCK unlock = {};
		unlock.hResource = lock.hResource;
		unlock.SubResourceIndex = lock.SubResourceIndex;
		unlock.Flags.NotifyOnly = 1;
		m_device.getOrigVtable().pfnUnlock(m_device, &unlock);
	}

	void Resource::onDestroyResource(HANDLE resource)
	{
		if (resource == m_handle ||
			m_msaaSurface.resource && *m_msaaSurface.resource == resource ||
			m_msaaResolvedSurface.resource && *m_msaaResolvedSurface.resource == resource)
		{
			loadSysMemResource(0);
			m_msaaSurface.resource = nullptr;
			m_msaaResolvedSurface.resource = nullptr;
		}
	}

	Resource& Resource::prepareForBltSrc(const D3DDDIARG_BLT& data)
	{
		if (m_lockResource || m_msaaResolvedSurface.resource)
		{
			loadVidMemResource(data.SrcSubResourceIndex);
		}
		return *this;
	}

	Resource& Resource::prepareForBltDst(D3DDDIARG_BLT& data)
	{
		return prepareForBltDst(data.hDstResource, data.DstSubResourceIndex, data.DstRect);
	}

	Resource& Resource::prepareForBltDst(HANDLE& resource, UINT subResourceIndex, RECT& rect)
	{
		if (m_lockResource || m_msaaResolvedSurface.resource)
		{
			loadFromLockRefResource(subResourceIndex);
			if (m_lockData[subResourceIndex].isMsaaUpToDate)
			{
				resource = *m_msaaSurface.resource;
				clearUpToDateFlags(subResourceIndex);
				m_lockData[subResourceIndex].isMsaaUpToDate = true;
				scaleRect(rect);
				return *m_msaaSurface.resource;
			}
			else if (m_lockData[subResourceIndex].isMsaaResolvedUpToDate)
			{
				resource = *m_msaaResolvedSurface.resource;
				clearUpToDateFlags(subResourceIndex);
				m_lockData[subResourceIndex].isMsaaResolvedUpToDate = true;
				scaleRect(rect);
				return *m_msaaResolvedSurface.resource;
			}
			else
			{
				loadVidMemResource(subResourceIndex);
				clearUpToDateFlags(subResourceIndex);
				m_lockData[subResourceIndex].isVidMemUpToDate = true;
			}
		}
		return *this;
	}

	void Resource::prepareForCpuRead(UINT subResourceIndex)
	{
		if (m_lockResource)
		{
			loadSysMemResource(subResourceIndex);
		}
	}

	void Resource::prepareForCpuWrite(UINT subResourceIndex)
	{
		m_isPaletteResolvedSurfaceUpToDate[subResourceIndex] = false;
		m_isColorKeyedSurfaceUpToDate[subResourceIndex] = false;
		if (m_lockResource)
		{
			if (m_lockRefSurface.resource &&
				(m_lockData[subResourceIndex].isMsaaResolvedUpToDate || m_lockData[subResourceIndex].isMsaaUpToDate))
			{
				loadVidMemResource(subResourceIndex);
				copySubResource(*m_lockRefSurface.resource, m_handle, subResourceIndex);
				m_lockData[subResourceIndex].isRefLocked = true;
			}

			loadSysMemResource(subResourceIndex);
			clearUpToDateFlags(subResourceIndex);
			m_lockData[subResourceIndex].isSysMemUpToDate = true;
		}
	}

	Resource& Resource::prepareForGpuRead(UINT subResourceIndex)
	{
		if (m_lockResource)
		{
			loadFromLockRefResource(subResourceIndex);
			if (m_msaaResolvedSurface.resource)
			{
				loadMsaaResolvedResource(subResourceIndex);
				return *m_msaaResolvedSurface.resource;
			}
			else
			{
				loadVidMemResource(subResourceIndex);
			}
		}
		return *this;
	}

	void Resource::prepareForGpuReadAll()
	{
		if (m_lockResource)
		{
			for (UINT i = 0; i < m_lockData.size(); ++i)
			{
				prepareForGpuRead(i);
			}
		}
	}

	Resource& Resource::prepareForGpuWrite(UINT subResourceIndex)
	{
		m_isColorKeyedSurfaceUpToDate[subResourceIndex] = false;
		if (m_lockResource || m_msaaResolvedSurface.resource)
		{
			if (m_msaaSurface.resource)
			{
				loadMsaaResource(subResourceIndex);
				clearUpToDateFlags(subResourceIndex);
				m_lockData[subResourceIndex].isMsaaUpToDate = true;
				return *m_msaaSurface.resource;
			}
			else if (m_msaaResolvedSurface.resource)
			{
				loadMsaaResolvedResource(subResourceIndex);
				clearUpToDateFlags(subResourceIndex);
				m_lockData[subResourceIndex].isMsaaResolvedUpToDate = true;
				return *m_msaaResolvedSurface.resource;
			}
			else
			{
				loadVidMemResource(subResourceIndex);
				clearUpToDateFlags(subResourceIndex);
				m_lockData[subResourceIndex].isVidMemUpToDate = true;
			}
		}
		return *this;
	}

	void Resource::prepareForGpuWriteAll()
	{
		if (m_lockResource)
		{
			for (UINT i = 0; i < m_lockData.size(); ++i)
			{
				prepareForGpuWrite(i);
			}
		}
	}

	Resource& Resource::prepareForTextureRead(UINT stage)
	{
		if (m_lockResource)
		{
			for (UINT i = 0; i < m_lockData.size(); ++i)
			{
				prepareForGpuRead(i);
			}
		}

		auto& defaultResource = m_msaaResolvedSurface.resource ? *m_msaaResolvedSurface.resource :
			(m_paletteResolvedSurface.resource ? *m_paletteResolvedSurface.resource : *this);
		const auto& appState = m_device.getState().getAppState();
		if (Config::Settings::ColorKeyMethod::ALPHATEST != Config::colorKeyMethod.get() ||
			!appState.renderState[D3DDDIRS_COLORKEYENABLE] ||
			appState.textureStageState[stage][D3DDDITSS_DISABLETEXTURECOLORKEY])
		{
			return defaultResource;
		}

		if (!m_colorKeyedSurface.surface)
		{
			m_device.getRepo().getSurface(m_colorKeyedSurface,
				m_fixedData.pSurfList[0].Width, m_fixedData.pSurfList[0].Height,
				D3DDDIFMT_A8R8G8B8, DDSCAPS_TEXTURE | DDSCAPS_3DDEVICE | DDSCAPS_VIDEOMEMORY |
				(m_fixedData.MipLevels > 1 ? DDSCAPS_MIPMAP : 0),
				m_fixedData.SurfCount,
				m_fixedData.Flags.CubeMap ? DDSCAPS2_CUBEMAP : 0);
			if (!m_colorKeyedSurface.surface)
			{
				return defaultResource;
			}
		}

		const auto colorKey = getMappedColorKey(appState.textureStageState[stage][D3DDDITSS_TEXTURECOLORKEYVAL]);
		if (colorKey != m_colorKey)
		{
			m_isColorKeyedSurfaceUpToDate.assign(m_fixedData.SurfCount, false);
			m_colorKey = colorKey;
		}

		const ShaderBlitter::ColorKeyInfo ck = { colorKey,
			D3DDDIFMT_P8 == m_origData.Format ? D3DDDIFMT_X8R8G8B8 : m_origData.Format };
		for (UINT i = 0; i < m_fixedData.SurfCount; ++i)
		{
			if (!m_isColorKeyedSurfaceUpToDate[i])
			{
				m_device.getShaderBlitter().colorKeyBlt(*m_colorKeyedSurface.resource, i, defaultResource, i, ck);
				m_isColorKeyedSurfaceUpToDate[i] = true;
			}
		}
		return *m_colorKeyedSurface.resource;
	}

	HRESULT Resource::presentationBlt(D3DDDIARG_BLT data, Resource* srcResource)
	{
		LOG_FUNC("Resource::presentationBlt", data, *srcResource);
		m_device.flushPrimitives();
		if (srcResource->m_lockResource)
		{
			auto& lockData = srcResource->m_lockData[data.SrcSubResourceIndex];
			const bool isSysMemOnly = lockData.isSysMemUpToDate && !lockData.isVidMemUpToDate;
			const bool isRefLocked = lockData.isRefLocked;
			const auto origSrcResource = srcResource;
			srcResource = &srcResource->prepareForGpuRead(data.SrcSubResourceIndex);
			if (isSysMemOnly)
			{
				lockData.isVidMemUpToDate = false;
				lockData.isMsaaResolvedUpToDate = false;
				if (isRefLocked)
				{
					origSrcResource->copySubResource(
						*origSrcResource->m_lockRefSurface.resource, *origSrcResource, data.SrcSubResourceIndex);
					lockData.isRefLocked = true;
				}
			}
		}
		prepareForGpuWrite(data.DstSubResourceIndex);

		LONG srcWidth = srcResource->m_fixedData.pSurfList[data.SrcSubResourceIndex].Width;
		LONG srcHeight = srcResource->m_fixedData.pSurfList[data.SrcSubResourceIndex].Height;
		data.SrcRect = { 0, 0, srcWidth, srcHeight };
		data.DstRect = m_device.getAdapter().applyDisplayAspectRatio(data.DstRect, { srcWidth, srcHeight });

		auto& repo = m_device.getRepo();
		const auto& srcRtt = repo.getPresentationSourceRtt(srcWidth, srcHeight, srcResource->m_fixedData.Format);
		if (!srcRtt.resource)
		{
			return LOG_RESULT(E_OUTOFMEMORY);
		}

		if (D3DDDIPOOL_SYSTEMMEM == srcResource->m_fixedData.Pool)
		{
			srcResource = repo.getTempTexture(srcWidth, srcHeight, srcResource->m_fixedData.Format).resource;
			if (!srcResource)
			{
				return LOG_RESULT(E_OUTOFMEMORY);
			}
			copySubResourceRegion(*srcResource, 0, data.SrcRect, data.hSrcResource, data.SrcSubResourceIndex, data.SrcRect);
		}

		if (D3DDDIFMT_P8 == srcResource->m_origData.Format ||
			D3DDDIFMT_L8 == srcResource->m_origData.Format)
		{
			auto entries(Gdi::Palette::getHardwarePalette());
			RGBQUAD pal[256] = {};
			for (UINT i = 0; i < 256; ++i)
			{
				pal[i].rgbRed = entries[i].peRed;
				pal[i].rgbGreen = entries[i].peGreen;
				pal[i].rgbBlue = entries[i].peBlue;
				pal[i].rgbReserved = 0xFF;
			}
			m_device.getShaderBlitter().palettizedBlt(
				*srcRtt.resource, 0, data.SrcRect, *srcResource, data.SrcSubResourceIndex, data.SrcRect, pal);
		}
		else
		{
			copySubResourceRegion(*srcRtt.resource, 0, data.SrcRect, *srcResource, data.SrcSubResourceIndex, data.SrcRect);
		}

		auto& mi = m_device.getAdapter().getMonitorInfo();
		presentLayeredWindows(*srcRtt.resource, 0, data.SrcRect, Gdi::Window::getVisibleLayeredWindows(), mi.rcEmulated);

		const auto cursorInfo = Gdi::Cursor::getEmulatedCursorInfo();
		const bool isCursorEmulated = cursorInfo.flags == CURSOR_SHOWING && cursorInfo.hCursor;
		if (isCursorEmulated)
		{
			m_device.getShaderBlitter().cursorBlt(*srcRtt.resource, 0, data.SrcRect, cursorInfo.hCursor, cursorInfo.ptScreenPos);
		}

		m_device.getShaderBlitter().displayBlt(*this, data.DstSubResourceIndex, data.DstRect, *srcRtt.resource, 0, data.SrcRect);
		clearRectExterior(data.DstSubResourceIndex, data.DstRect);

		presentLayeredWindows(*this, data.DstSubResourceIndex, getRect(data.DstSubResourceIndex),
			Gdi::Window::getVisibleOverlayWindows(), m_device.getAdapter().getMonitorInfo().rcMonitor);

		Overlay::Steam::render(*this, data.DstSubResourceIndex);

		return LOG_RESULT(S_OK);
	}

	void Resource::presentLayeredWindows(Resource& dst, UINT dstSubResourceIndex, const RECT& dstRect,
		std::vector<Gdi::Window::LayeredWindow> layeredWindows, const RECT& monitorRect)
	{
		auto& blitter = m_device.getShaderBlitter();
		auto& repo = m_device.getRepo();

		for (auto& layeredWindow : layeredWindows)
		{
			RECT visibleRect = {};
			IntersectRect(&visibleRect, &layeredWindow.rect, &monitorRect);
			if (IsRectEmpty(&visibleRect))
			{
				continue;
			}

			COLORREF colorKey = 0;
			BYTE alpha = 0;
			DWORD flags = 0;
			if (layeredWindow.dc)
			{
				colorKey = layeredWindow.colorKey;
				alpha = layeredWindow.alpha;
				if (CLR_INVALID != colorKey)
				{
					flags |= ULW_COLORKEY;
				}
				if (255 != alpha)
				{
					flags |= ULW_ALPHA;
				}
			}
			else
			{
				GetLayeredWindowAttributes(layeredWindow.hwnd, &colorKey, &alpha, &flags);
			}

			RECT srcRect = { 0, 0, visibleRect.right - visibleRect.left + 1, visibleRect.bottom - visibleRect.top + 1 };
			auto& windowSurface = repo.getTempSysMemSurface(srcRect.right, srcRect.bottom);
			auto& texture = repo.getTempTexture(srcRect.right, srcRect.bottom, D3DDDIFMT_A8R8G8B8);
			if (!windowSurface.resource || !texture.resource)
			{
				continue;
			}

			HDC srcDc = layeredWindow.dc ? layeredWindow.dc : GetWindowDC(layeredWindow.hwnd);
			HDC dstDc = nullptr;
			POINT srcOrig = { visibleRect.left - layeredWindow.rect.left, visibleRect.top - layeredWindow.rect.top };
			windowSurface.surface->GetDC(windowSurface.surface, &dstDc);

			CALL_ORIG_FUNC(BitBlt)(dstDc, 0, 0, srcRect.right - 1, srcRect.bottom - 1,
				srcDc, srcOrig.x, srcOrig.y, SRCCOPY);
			CALL_ORIG_FUNC(BitBlt)(dstDc, srcRect.right - 1, 0, 1, srcRect.bottom - 1,
				srcDc, srcOrig.x + srcRect.right - 2, srcOrig.y, SRCCOPY);
			CALL_ORIG_FUNC(BitBlt)(dstDc, 0, srcRect.bottom - 1, srcRect.right - 1, 1,
				srcDc, srcOrig.x, srcOrig.y + srcRect.bottom - 2, SRCCOPY);
			CALL_ORIG_FUNC(BitBlt)(dstDc, srcRect.right - 1, srcRect.bottom - 1, 1, 1,
				srcDc, srcOrig.x + srcRect.right - 2, srcOrig.y + srcRect.bottom - 2, SRCCOPY);

			windowSurface.surface->ReleaseDC(windowSurface.surface, dstDc);
			if (!layeredWindow.dc)
			{
				ReleaseDC(layeredWindow.hwnd, srcDc);
			}

			copySubResourceRegion(*texture.resource, 0, srcRect, *windowSurface.resource, 0, srcRect);
			windowSurface.resource->notifyLock(0);

			const ShaderBlitter::ColorKeyInfo ck = { colorKey,
				(flags & ULW_COLORKEY) ? D3DDDIFMT_A8R8G8B8 : D3DDDIFMT_UNKNOWN };

			if (layeredWindow.region)
			{
				layeredWindow.region &= monitorRect;
				layeredWindow.region.offset(-visibleRect.left, -visibleRect.top);
			}
			Rect::transform(visibleRect, monitorRect, dstRect);

			srcRect.right--;
			srcRect.bottom--;
			if (layeredWindow.dc)
			{
				blitter.alphaBlendBlt(dst, dstSubResourceIndex, visibleRect, *texture.resource, 0, srcRect,
					ck, (flags & ULW_ALPHA) ? alpha : 255, layeredWindow.region);
			}
			else
			{
				blitter.textureBlt(dst, dstSubResourceIndex, visibleRect, *texture.resource, 0, srcRect,
					D3DTEXF_LINEAR | D3DTEXF_SRGB, ck, (flags & ULW_ALPHA) ? &alpha : nullptr,
					layeredWindow.region);
			}
		}
	}

	void Resource::resolveMsaaDepthBuffer()
	{
		LOG_FUNC("Resource::resolveMsaaDepthBuffer");
		auto& state = m_device.getState();
		state.setTempDepthStencil({ *m_msaaSurface.resource });
		state.setTempTexture(0, *m_msaaResolvedSurface.resource);

		const UINT RESZ_CODE = 0x7fa05000;
		state.setTempRenderState({ D3DDDIRS_POINTSIZE, RESZ_CODE });
	}

	void Resource::scaleRect(RECT& rect)
	{
		const LONG origWidth = m_fixedData.pSurfList[0].Width;
		const LONG origHeight = m_fixedData.pSurfList[0].Height;

		rect.left = rect.left * m_scaledSize.cx / origWidth;
		rect.top = rect.top * m_scaledSize.cy / origHeight;
		rect.right = rect.right * m_scaledSize.cx / origWidth;
		rect.bottom = rect.bottom * m_scaledSize.cy / origHeight;
	}

	void Resource::setAsGdiResource(bool isGdiResource)
	{
		m_device.flushPrimitives();
		m_lockResource.reset();
		m_lockData.clear();
		m_lockBuffer.reset();
		if (isGdiResource)
		{
			createGdiLockResource();
		}
		else
		{
			createLockResource();
		}
	}

	void Resource::setAsPrimary()
	{
		D3dDdi::ScopedCriticalSection lock;
		m_isPrimary = true;
		m_origData.Flags.RenderTarget = DDraw::PrimarySurface::getOrigCaps() & DDSCAPS_3DDEVICE ? 1 : 0;
		updateConfig();
	}

	void Resource::setFormatOverride(D3DDDIFORMAT format)
	{
		g_formatOverride = format;
	}

	void Resource::setPaletteHandle(UINT paletteHandle)
	{
		if (paletteHandle != m_paletteHandle)
		{
			m_paletteHandle = paletteHandle;
			invalidatePalettizedTexture();
		}
	}

	void Resource::setReadOnlyLock(bool readOnly)
	{
		g_readOnlyLock = readOnly;
	}

	HRESULT Resource::shaderBlt(const D3DDDIARG_BLT& data, Resource& dstResource, Resource& srcResource, UINT filter)
	{
		LOG_FUNC("Resource::shaderBlt", data, dstResource, srcResource);

		if (D3DDDIPOOL_SYSTEMMEM == dstResource.m_fixedData.Pool ||
			dstResource.canCopySubResource(data, srcResource))
		{
			HRESULT result = m_device.getOrigVtable().pfnBlt(m_device, &data);
			if (SUCCEEDED(result) && D3DDDIPOOL_SYSTEMMEM == dstResource.m_fixedData.Pool)
			{
				dstResource.notifyLock(data.DstSubResourceIndex);
			}
			return LOG_RESULT(result);
		}

		auto& repo = m_device.getRepo();

		Resource* srcRes = &srcResource;
		UINT srcIndex = data.SrcSubResourceIndex;
		RECT srcRect = data.SrcRect;

		Resource* dstRes = &dstResource;
		UINT dstIndex = data.DstSubResourceIndex;
		RECT dstRect = data.DstRect;

		if (D3DTEXF_POINT != filter &&
			(dstResource.m_fixedData.Flags.ZBuffer || Rect::isEqualSize(srcRect, dstRect)))
		{
			filter = D3DTEXF_POINT;
		}

		if (!srcResource.m_fixedData.Flags.Texture ||
			D3DDDIPOOL_SYSTEMMEM == srcResource.m_fixedData.Pool ||
			(filter & D3DTEXF_SRGBREAD) && !(srcResource.m_formatOp.Operations & FORMATOP_SRGBREAD))
		{
			DWORD width = data.SrcRect.right - data.SrcRect.left;
			DWORD height = data.SrcRect.bottom - data.SrcRect.top;
			auto texture = m_fixedData.Flags.ZBuffer
				? m_msaaResolvedSurface.resource
				: repo.getTempTexture(width, height, srcResource.m_fixedData.Format).resource;
			if (!texture)
			{
				return LOG_RESULT(E_OUTOFMEMORY);
			}

			srcRes = texture;
			srcIndex = 0;
			srcRect = { 0, 0, static_cast<LONG>(width), static_cast<LONG>(height) };

			HRESULT result = copySubResourceRegion(*srcRes, srcIndex, srcRect,
				data.hSrcResource, data.SrcSubResourceIndex, data.SrcRect);
			if (FAILED(result))
			{
				return LOG_RESULT(result);
			}

			if (D3DDDIPOOL_SYSTEMMEM == srcResource.m_fixedData.Pool)
			{
				srcResource.notifyLock(data.SrcSubResourceIndex);
			}
		}

		if (!dstResource.m_fixedData.Flags.RenderTarget && !dstResource.m_fixedData.Flags.ZBuffer ||
			(filter & D3DTEXF_SRGBWRITE) && !(dstResource.m_formatOp.Operations & FORMATOP_SRGBWRITE))
		{
			LONG width = data.DstRect.right - data.DstRect.left;
			LONG height = data.DstRect.bottom - data.DstRect.top;
			auto& rt = repo.getNextRenderTarget(width, height, D3DDDIFMT_X8R8G8B8, srcRes);
			if (!rt.resource)
			{
				return LOG_RESULT(E_OUTOFMEMORY);
			}

			dstRes = rt.resource;
			dstIndex = 0;
			dstRect = { 0, 0, width, height };

			if (data.Flags.SrcColorKey)
			{
				HRESULT result = copySubResourceRegion(*dstRes, dstIndex, dstRect,
					data.hDstResource, data.DstSubResourceIndex, data.DstRect);
				if (FAILED(result))
				{
					return LOG_RESULT(result);
				}
			}
		}

		if (data.Flags.MirrorLeftRight)
		{
			std::swap(srcRect.left, srcRect.right);
		}

		if (data.Flags.MirrorUpDown)
		{
			std::swap(srcRect.top, srcRect.bottom);
		}

		if (m_fixedData.Flags.ZBuffer)
		{
			const bool isD3D9On12 = m_device.getAdapter().getInfo().isD3D9On12;
			if (isD3D9On12 ||
				m_nullRt.resource && isDepthTexture(srcRes->m_fixedData.Format))
			{
				m_device.getShaderBlitter().depthBlt(*dstRes, dstRect, *srcRes, srcRect);
			}
		}
		else
		{
			if ((D3DTEXF_LINEAR | D3DTEXF_SRGB) == filter)
			{
				m_device.getShaderBlitter().bilinearBlt(*dstRes, dstIndex, dstRect, *srcRes, srcIndex, srcRect, 0);
			}
			else
			{
				const ShaderBlitter::ColorKeyInfo ck = { data.ColorKey,
					data.Flags.SrcColorKey ? srcResource.m_origData.Format : D3DDDIFMT_UNKNOWN };
				m_device.getShaderBlitter().textureBlt(*dstRes, dstIndex, dstRect, *srcRes, srcIndex, srcRect, filter, ck);
			}
		}

		if (*dstRes != data.hDstResource)
		{
			HRESULT result = copySubResourceRegion(data.hDstResource, data.DstSubResourceIndex, data.DstRect,
				*dstRes, dstIndex, dstRect);
			if (FAILED(result))
			{
				return LOG_RESULT(result);
			}
		}

		return LOG_RESULT(S_OK);
	}

	bool Resource::shouldBltViaCpu(const D3DDDIARG_BLT& data, Resource& srcResource)
	{
		if (m_fixedData.Format != srcResource.m_fixedData.Format ||
			0 == m_formatInfo.bytesPerPixel ||
			m_fixedData.Flags.ZBuffer ||
			D3DDDIPOOL_SYSTEMMEM != srcResource.m_fixedData.Pool && !srcResource.m_lockResource)
		{
			return false;
		}

		if (D3DDDIPOOL_SYSTEMMEM == m_fixedData.Pool ||
			D3DDDIFMT_P8 == m_fixedData.Format ||
			m_isOversized || srcResource.m_isOversized)
		{
			return true;
		}

		if (m_isPrimary)
		{
			auto origCaps = DDraw::PrimarySurface::getOrigCaps();
			if ((origCaps & DDSCAPS_SYSTEMMEMORY) && !(origCaps & DDSCAPS_FLIP))
			{
				return true;
			}
		}

		if (m_lockData.empty() ||
			!m_lockData[data.DstSubResourceIndex].isSysMemUpToDate ||
			Time::qpcToMs(Time::queryPerformanceCounter() - m_lockData[data.DstSubResourceIndex].qpcLastCpuAccess) > 200)
		{
			return false;
		}

		return Config::Settings::BltFilter::POINT == Config::bltFilter.get() || Rect::isEqualSize(data.SrcRect, data.DstRect);
	}

	HRESULT Resource::unlock(const D3DDDIARG_UNLOCK& data)
	{
		if (m_lockResource && m_origData.Flags.Texture)
		{
			m_device.getState().unlockTexture(*this);
		}
		return (m_lockResource || m_isOversized) ? S_OK : m_device.getOrigVtable().pfnUnlock(m_device, &data);
	}

	void Resource::updateConfig()
	{
		if (m_isSurfaceRepoResource || D3DDDIPOOL_SYSTEMMEM == m_fixedData.Pool || D3DDDIFMT_P8 == m_fixedData.Format ||
			m_fixedData.Flags.MatchGdiPrimary ||
			!m_isPrimary && !m_origData.Flags.RenderTarget && !m_fixedData.Flags.ZBuffer ||
			!m_fixedData.Flags.ZBuffer && !m_lockResource ||
			m_fixedData.MipLevels > 1)
		{
			return;
		}

		const auto msaa = getMultisampleConfig();
		const auto formatConfig = getFormatConfig();
		const auto scaledSize = getScaledSize();

		if (m_multiSampleConfig == msaa && m_formatConfig == formatConfig && m_scaledSize == scaledSize)
		{
			return;
		}

		if (m_msaaSurface.resource || m_msaaResolvedSurface.resource)
		{
			for (UINT i = 0; i < m_lockData.size(); ++i)
			{
				if (m_lockData[i].isMsaaUpToDate || m_lockData[i].isMsaaResolvedUpToDate)
				{
					loadVidMemResource(i);
				}
				m_lockData[i].isMsaaUpToDate = false;
				m_lockData[i].isMsaaResolvedUpToDate = false;
				m_lockData[i].isRefLocked = false;
			}
		}

		m_multiSampleConfig = msaa;
		m_formatConfig = formatConfig;
		m_scaledSize = scaledSize;

		m_msaaSurface = {};
		m_msaaResolvedSurface = {};
		m_lockRefSurface = {};

		const bool isScaled = static_cast<LONG>(m_fixedData.pSurfList[0].Width) != m_scaledSize.cx ||
			static_cast<LONG>(m_fixedData.pSurfList[0].Height) != m_scaledSize.cy;
		if (D3DDDIMULTISAMPLE_NONE != msaa.first || m_fixedData.Format != formatConfig || isScaled)
		{
			auto& repo = m_device.getRepo();
			const DWORD caps = (m_fixedData.Flags.ZBuffer ? DDSCAPS_ZBUFFER : DDSCAPS_3DDEVICE) | DDSCAPS_VIDEOMEMORY;
			if (D3DDDIMULTISAMPLE_NONE != msaa.first)
			{
				g_msaaOverride = msaa;
				repo.getSurface(m_msaaSurface, scaledSize.cx, scaledSize.cy, formatConfig, caps, m_fixedData.SurfCount);
				g_msaaOverride = {};
			}

			repo.getSurface(m_msaaResolvedSurface, scaledSize.cx, scaledSize.cy,
				formatConfig, caps, m_fixedData.SurfCount);

			if (!m_msaaResolvedSurface.resource && m_msaaSurface.resource)
			{
				m_msaaSurface = {};
				repo.getSurface(m_msaaResolvedSurface, scaledSize.cx, scaledSize.cy,
					formatConfig, caps, m_fixedData.SurfCount);
			}

			if (!m_fixedData.Flags.ZBuffer && m_msaaResolvedSurface.resource)
			{
				repo.getSurface(m_lockRefSurface, m_fixedData.pSurfList[0].Width, m_fixedData.pSurfList[0].Height,
					m_fixedData.Format, DDSCAPS_TEXTURE | DDSCAPS_VIDEOMEMORY, m_fixedData.SurfCount);
				
				if (isScaled && m_device.getGdiResource() == this)
				{
					loadMsaaResolvedResource(0);
					m_lockData[0].isVidMemUpToDate = false;
					loadVidMemResource(0);
				}
			}
		}
	}

	void Resource::updatePalettizedTexture()
	{
		if (!isPalettizedTexture())
		{
			return;
		}

		prepareForGpuReadAll();
		if (m_paletteResolvedSurface.resource)
		{
			for (unsigned i = 0; i < m_fixedData.SurfCount; ++i)
			{
				if (!m_isPaletteResolvedSurfaceUpToDate[i])
				{
					auto rect = getRect(i);
					m_device.getShaderBlitter().palettizedBlt(*m_paletteResolvedSurface.resource, i, rect, *this, i, rect,
						m_device.getPalette(m_paletteHandle));
					m_isPaletteResolvedSurfaceUpToDate[i] = true;
				}
			}
		}
	}
}
