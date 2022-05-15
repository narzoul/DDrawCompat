#include <type_traits>

#include <Common/Comparison.h>
#include <Common/HResultException.h>
#include <Common/Log.h>
#include <Common/Rect.h>
#include <Common/Time.h>
#include <Config/Config.h>
#include <D3dDdi/Adapter.h>
#include <D3dDdi/Device.h>
#include <D3dDdi/KernelModeThunks.h>
#include <D3dDdi/Log/DeviceFuncsLog.h>
#include <D3dDdi/Resource.h>
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
#include <Win32/DisplayMode.h>

namespace
{
	D3DDDI_RESOURCEFLAGS getResourceTypeFlags();

	const UINT g_resourceTypeFlags = getResourceTypeFlags().Value;
	RECT g_presentationRect = {};
	
	D3DDDIFORMAT g_formatOverride = D3DDDIFMT_UNKNOWN;
	std::pair<D3DDDIMULTISAMPLE_TYPE, UINT> g_msaaOverride = {};

	RECT calculateScaledRect(const RECT& srcRect, const RECT& dstRect)
	{
		const int srcWidth = srcRect.right - srcRect.left;
		const int srcHeight = srcRect.bottom - srcRect.top;
		const int dstWidth = dstRect.right - dstRect.left;
		const int dstHeight = dstRect.bottom - dstRect.top;

		RECT rect = { 0, 0, dstWidth, dstHeight };
		if (dstWidth * srcHeight > dstHeight * srcWidth)
		{
			rect.right = dstHeight * srcWidth / srcHeight;
		}
		else
		{
			rect.bottom = dstWidth * srcHeight / srcWidth;
		}

		OffsetRect(&rect, (dstWidth - rect.right) / 2, (dstHeight - rect.bottom) / 2);
		return rect;
	}

	RECT calculatePresentationRect()
	{
		return calculateScaledRect(DDraw::PrimarySurface::getMonitorRect(), DDraw::RealPrimarySurface::getMonitorRect());
	}

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
		, m_lockBuffer(nullptr, &heapFree)
		, m_lockResource(nullptr, ResourceDeleter(device, device.getOrigVtable().pfnDestroyResource))
		, m_msaaSurface{}
		, m_msaaResolvedSurface{}
		, m_formatConfig(D3DDDIFMT_UNKNOWN)
		, m_multiSampleConfig{ D3DDDIMULTISAMPLE_NONE, 0 }
		, m_scaledSize{}
		, m_isOversized(false)
		, m_isSurfaceRepoResource(SurfaceRepository::inCreateSurface())
		, m_isClampable(true)
	{
		if (m_origData.Flags.VertexBuffer &&
			m_origData.Flags.MightDrawFromLocked &&
			D3DDDIPOOL_SYSTEMMEM != m_origData.Pool)
		{
			throw HResultException(E_FAIL);
		}

		if (m_origData.Flags.MatchGdiPrimary)
		{
			g_presentationRect = calculatePresentationRect();
			auto& si = m_origData.pSurfList[0];
			RECT primaryRect = { 0, 0, static_cast<LONG>(si.Width), static_cast<LONG>(si.Height) };

			Gdi::Cursor::setMonitorClipRect(DDraw::PrimarySurface::getMonitorRect());
			if (!EqualRect(&g_presentationRect, &primaryRect))
			{
				Gdi::Cursor::setEmulated(true);
			}
			Gdi::VirtualScreen::setFullscreenMode(true);
		}

		fixResourceData();
		m_formatInfo = getFormatInfo(m_fixedData.Format);
		m_formatConfig = m_fixedData.Format;
		m_scaledSize = { static_cast<LONG>(m_fixedData.pSurfList[0].Width), static_cast<LONG>(m_fixedData.pSurfList[0].Height) };

		HRESULT result = m_device.createPrivateResource(m_fixedData);
		if (FAILED(result))
		{
			throw HResultException(result);
		}
		m_handle = m_fixedData.hResource;

		updateConfig();

		if (D3DDDIPOOL_SYSTEMMEM == m_fixedData.Pool &&
			0 != m_formatInfo.bytesPerPixel)
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

		data.hResource = m_fixedData.hResource;
	}

	Resource::~Resource()
	{
		if (m_origData.Flags.MatchGdiPrimary)
		{
			Gdi::VirtualScreen::setFullscreenMode(false);
			Gdi::Cursor::setEmulated(false);
			Gdi::Cursor::setMonitorClipRect({});
		}

		if (m_msaaSurface.surface || m_msaaResolvedSurface.surface)
		{
			auto& repo = SurfaceRepository::get(m_device.getAdapter());
			repo.release(m_msaaSurface);
			repo.release(m_msaaResolvedSurface);
		}
	}

	HRESULT Resource::blt(D3DDDIARG_BLT data)
	{
		if (!isValidRect(data.DstSubResourceIndex, data.DstRect))
		{
			return S_OK;
		}

		DDraw::setBltSrc(data);
		auto srcResource = m_device.getResource(data.hSrcResource);
		if (srcResource)
		{
			if (!srcResource->isValidRect(data.SrcSubResourceIndex, data.SrcRect))
			{
				return S_OK;
			}

			if (D3DDDIPOOL_SYSTEMMEM == m_fixedData.Pool &&
				D3DDDIPOOL_SYSTEMMEM == srcResource->m_fixedData.Pool)
			{
				return m_device.getOrigVtable().pfnBlt(m_device, &data);
			}
		}

		if (srcResource)
		{
			if (m_fixedData.Flags.MatchGdiPrimary)
			{
				return presentationBlt(data, srcResource);
			}

			HRESULT result = bltViaCpu(data, *srcResource);
			if (S_FALSE != result)
			{
				return result;
			}

			return bltViaGpu(data, *srcResource);
		}
		
		prepareForBltDst(data);
		return m_device.getOrigVtable().pfnBlt(m_device, &data);
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
		if (m_fixedData.Format != srcResource.m_fixedData.Format ||
			0 == m_formatInfo.bytesPerPixel ||
			D3DDDIFMT_P8 != m_fixedData.Format && !m_isOversized && !srcResource.m_isOversized)
		{
			return S_FALSE;
		}

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
		if (D3DDDIPOOL_SYSTEMMEM != m_fixedData.Pool &&
			(m_fixedData.Flags.RenderTarget || data.Flags.SrcColorKey || data.Flags.MirrorLeftRight || data.Flags.MirrorUpDown))
		{
			if (SUCCEEDED(shaderBlt(data, srcResource)))
			{
				return S_OK;
			}
		}
		else
		{
			srcResource.prepareForBltSrc(data);
			prepareForBltDst(data);
		}

		auto bltFilter = Config::bltFilter.get();
		if (Config::Settings::BltFilter::NATIVE != bltFilter &&
			D3DDDIPOOL_SYSTEMMEM != m_fixedData.Pool &&
			D3DDDIPOOL_SYSTEMMEM != srcResource.m_fixedData.Pool)
		{
			if (Config::Settings::BltFilter::POINT == bltFilter)
			{
				data.Flags.Point = 1;
			}
			else
			{
				data.Flags.Linear = 1;
			}
		}

		HRESULT result = m_device.getOrigVtable().pfnBlt(m_device, &data);
		if (D3DDDIPOOL_SYSTEMMEM == m_fixedData.Pool)
		{
			notifyLock(data.DstSubResourceIndex);
		}
		else if (D3DDDIPOOL_SYSTEMMEM == srcResource.m_fixedData.Pool)
		{
			srcResource.notifyLock(data.SrcSubResourceIndex);
		}
		return result;
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
			auto& lockData = m_lockData[data.SubResourceIndex];
			if (lockData.isSysMemUpToDate && !lockData.isVidMemUpToDate)
			{
				auto dstBuf = static_cast<BYTE*>(lockData.data) +
					data.DstRect.top * lockData.pitch + data.DstRect.left * m_formatInfo.bytesPerPixel;

				DDraw::Blitter::colorFill(dstBuf, lockData.pitch,
					data.DstRect.right - data.DstRect.left, data.DstRect.bottom - data.DstRect.top,
					m_formatInfo.bytesPerPixel, colorConvert(m_formatInfo, data.Color));

				return LOG_RESULT(S_OK);
			}
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
		auto gdiSurfaceDesc(Gdi::VirtualScreen::getSurfaceDesc(DDraw::PrimarySurface::getMonitorRect()));
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
		if (D3DDDIPOOL_SYSTEMMEM == m_fixedData.Pool ||
			m_isSurfaceRepoResource ||
			0 == m_formatInfo.bytesPerPixel ||
			0 != (m_fixedData.Flags.Value & flags.Value))
		{
			return;
		}

		std::vector<D3DDDI_SURFACEINFO> surfaceInfo(m_fixedData.SurfCount);
		for (UINT i = 0; i < m_fixedData.SurfCount; ++i)
		{
			surfaceInfo[i].Width = m_fixedData.pSurfList[i].Width;
			surfaceInfo[i].Height = m_fixedData.pSurfList[i].Height;
			surfaceInfo[i].SysMemPitch = (surfaceInfo[i].Width * m_formatInfo.bytesPerPixel + 3) & ~3;
			if (i != 0)
			{
				std::uintptr_t offset = reinterpret_cast<std::uintptr_t>(surfaceInfo[i - 1].pSysMem) +
					((surfaceInfo[i - 1].SysMemPitch * surfaceInfo[i - 1].Height + 15) & ~15);
				surfaceInfo[i].pSysMem = reinterpret_cast<void*>(offset);
			}
		}

		std::uintptr_t bufferSize = reinterpret_cast<std::uintptr_t>(surfaceInfo.back().pSysMem) +
			surfaceInfo.back().SysMemPitch * surfaceInfo.back().Height + 8;
		m_lockBuffer.reset(HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, bufferSize));

		BYTE* bufferStart = static_cast<BYTE*>(m_lockBuffer.get());
		if (0 == reinterpret_cast<std::uintptr_t>(bufferStart) % 16)
		{
			bufferStart += 8;
		}

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
			}
		}

#ifdef DEBUGLOGS
		LOG_RESULT(m_lockResource.get());
#endif
	}

	void Resource::disableClamp()
	{
		m_isClampable = false;
	}

	bool Resource::downscale(Resource*& rt, LONG& srcWidth, LONG& srcHeight, LONG dstWidth, LONG dstHeight)
	{
		LONG newSrcWidth = (srcWidth + 1) / 2;
		if (newSrcWidth <= dstWidth)
		{
			newSrcWidth = srcWidth;
		}

		LONG newSrcHeight = (srcHeight + 1) / 2;
		if (newSrcHeight <= dstHeight)
		{
			newSrcHeight = srcHeight;
		}

		if (newSrcWidth == srcWidth && newSrcHeight == srcHeight)
		{
			return false;
		}

		auto& nextRt = getNextRenderTarget(rt, newSrcWidth, newSrcHeight);
		if (!nextRt.resource)
		{
			return false;
		}

		m_device.getShaderBlitter().textureBlt(*nextRt.resource, 0, { 0, 0, newSrcWidth, newSrcHeight },
			*rt, 0, { 0, 0, srcWidth, srcHeight }, D3DTEXF_LINEAR);
		rt = nextRt.resource;
		srcWidth = newSrcWidth;
		srcHeight = newSrcHeight;
		return true;
	}

	void Resource::fixResourceData()
	{
		if (m_fixedData.Flags.MatchGdiPrimary)
		{
			RECT r = DDraw::RealPrimarySurface::getMonitorRect();
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

		if (D3DDDIFMT_UNKNOWN != g_formatOverride)
		{
			m_fixedData.Format = g_formatOverride;
		}

		if (D3DDDIMULTISAMPLE_NONE != g_msaaOverride.first)
		{
			m_fixedData.MultisampleType = g_msaaOverride.first;
			m_fixedData.MultisampleQuality = g_msaaOverride.second;
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
		if (m_fixedData.Flags.RenderTarget && !m_fixedData.Flags.MatchGdiPrimary && D3DDDIPOOL_SYSTEMMEM != m_fixedData.Pool &&
			(D3DDDIFMT_X8R8G8B8 == m_fixedData.Format || D3DDDIFMT_R5G6B5 == m_fixedData.Format))
		{
			switch (Config::renderColorDepth.get())
			{
			case 16: return D3DDDIFMT_R5G6B5;
			case 32: return D3DDDIFMT_X8R8G8B8;
			}
		}
		return m_fixedData.Format;
	}

	void* Resource::getLockPtr(UINT subResourceIndex)
	{
		return m_lockData.empty() ? nullptr : m_lockData[subResourceIndex].data;
	}

	std::pair<D3DDDIMULTISAMPLE_TYPE, UINT> Resource::getMultisampleConfig()
	{
		if ((m_fixedData.Flags.RenderTarget && !m_fixedData.Flags.Texture && !m_fixedData.Flags.MatchGdiPrimary ||
			m_fixedData.Flags.ZBuffer) &&
			D3DDDIPOOL_SYSTEMMEM != m_fixedData.Pool)
		{
			return m_device.getAdapter().getMultisampleConfig(m_fixedData.Format);
		}
		return { D3DDDIMULTISAMPLE_NONE, 0 };
	}

	const SurfaceRepository::Surface& Resource::getNextRenderTarget(Resource* currentRt, DWORD width, DWORD height)
	{
		auto& repo = SurfaceRepository::get(m_device.getAdapter());
		auto nextRt = &repo.getTempRenderTarget(width, height, 1);
		if (nextRt->resource == currentRt)
		{
			nextRt = &repo.getTempRenderTarget(width, height, 0);
		}
		return *nextRt;
	}

	RECT Resource::getRect(UINT subResourceIndex)
	{
		const auto& si = m_fixedData.pSurfList[subResourceIndex];
		return { 0, 0, static_cast<LONG>(si.Width), static_cast<LONG>(si.Height) };
	}

	SIZE Resource::getScaledSize()
	{
		SIZE size = { static_cast<LONG>(m_fixedData.pSurfList[0].Width), static_cast<LONG>(m_fixedData.pSurfList[0].Height) };
		if ((m_fixedData.Flags.RenderTarget && !m_fixedData.Flags.Texture && !m_fixedData.Flags.MatchGdiPrimary ||
			m_fixedData.Flags.ZBuffer) &&
			D3DDDIPOOL_SYSTEMMEM != m_fixedData.Pool)
		{
			return m_device.getAdapter().getScaledSize(size);
		}
		return size;
	}

	bool Resource::isValidRect(UINT subResourceIndex, const RECT& rect)
	{
		return rect.left >= 0 && rect.top >= 0 && rect.left < rect.right && rect.top < rect.bottom &&
			rect.right <= static_cast<LONG>(m_fixedData.pSurfList[subResourceIndex].Width) &&
			rect.bottom <= static_cast<LONG>(m_fixedData.pSurfList[subResourceIndex].Height);
	}

	void Resource::loadMsaaResource(UINT subResourceIndex)
	{
		if (!m_lockData[subResourceIndex].isMsaaUpToDate)
		{
			if (m_msaaResolvedSurface.resource)
			{
				loadMsaaResolvedResource(subResourceIndex);
				copySubResource(*m_msaaSurface.resource, *m_msaaResolvedSurface.resource, subResourceIndex);
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
		if (!m_lockData[subResourceIndex].isMsaaResolvedUpToDate)
		{
			if (m_lockData[subResourceIndex].isMsaaUpToDate)
			{
				copySubResource(*m_msaaResolvedSurface.resource, *m_msaaSurface.resource, subResourceIndex);
			}
			else
			{
				loadVidMemResource(subResourceIndex);
				copySubResource(*m_msaaResolvedSurface.resource, *this, subResourceIndex);
			}
			m_lockData[subResourceIndex].isMsaaResolvedUpToDate = true;
		}
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
	}

	void Resource::loadVidMemResource(UINT subResourceIndex)
	{
		if (!m_lockData[subResourceIndex].isVidMemUpToDate)
		{
			if (m_lockData[subResourceIndex].isMsaaUpToDate || m_lockData[subResourceIndex].isMsaaResolvedUpToDate)
			{
				if (m_msaaResolvedSurface.resource)
				{
					loadMsaaResolvedResource(subResourceIndex);
					copySubResource(*this, *m_msaaResolvedSurface.resource, subResourceIndex);
				}
				else
				{
					copySubResource(*this, *m_msaaSurface.resource, subResourceIndex);
				}
			}
			else
			{
				copySubResource(*this, m_lockResource.get(), subResourceIndex);
				notifyLock(subResourceIndex);
			}
			m_lockData[subResourceIndex].isVidMemUpToDate = true;
		}
	}

	HRESULT Resource::lock(D3DDDIARG_LOCK& data)
	{
		if (D3DDDIMULTISAMPLE_NONE != m_fixedData.MultisampleType)
		{
			return E_FAIL;
		}

		D3DDDIARG_BLT blt = {};
		DDraw::setBltSrc(blt);
		if (blt.hSrcResource)
		{
			return E_ABORT;
		}

		if (m_lockResource || m_isOversized)
		{
			return bltLock(data);
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
		}
	}

	Resource& Resource::prepareForBltSrc(const D3DDDIARG_BLT& data)
	{
		if (m_lockResource)
		{
			loadVidMemResource(data.SrcSubResourceIndex);
		}
		return *this;
	}

	Resource& Resource::prepareForBltDst(D3DDDIARG_BLT& data)
	{
		return prepareForBltDst(data.hDstResource, data.DstSubResourceIndex, data.DstRect);
	}

	Resource& Resource::prepareForBltDst(HANDLE& resource, UINT& subResourceIndex, RECT& rect)
	{
		if (m_lockResource)
		{
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
		if (m_lockResource)
		{
			loadSysMemResource(subResourceIndex);
			clearUpToDateFlags(subResourceIndex);
			m_lockData[subResourceIndex].isSysMemUpToDate = true;
		}
	}

	Resource& Resource::prepareForGpuRead(UINT subResourceIndex)
	{
		if (m_lockResource)
		{
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

	void Resource::prepareForGpuWrite(UINT subResourceIndex)
	{
		if (m_lockResource)
		{
			if (m_msaaSurface.resource)
			{
				loadMsaaResource(subResourceIndex);
				clearUpToDateFlags(subResourceIndex);
				m_lockData[subResourceIndex].isMsaaUpToDate = true;
			}
			else if (m_msaaResolvedSurface.resource)
			{
				loadMsaaResolvedResource(subResourceIndex);
				clearUpToDateFlags(subResourceIndex);
				m_lockData[subResourceIndex].isMsaaResolvedUpToDate = true;
			}
			else
			{
				loadVidMemResource(subResourceIndex);
				clearUpToDateFlags(subResourceIndex);
				m_lockData[subResourceIndex].isVidMemUpToDate = true;
			}
		}
	}

	HRESULT Resource::presentationBlt(D3DDDIARG_BLT data, Resource* srcResource)
	{
		if (srcResource->m_lockResource)
		{
			if (srcResource->m_lockData[0].isSysMemUpToDate &&
				!srcResource->m_fixedData.Flags.RenderTarget)
			{
				srcResource->m_lockData[0].isVidMemUpToDate = false;
			}

			srcResource = &srcResource->prepareForGpuRead(0);
		}

		LONG srcWidth = srcResource->m_fixedData.pSurfList[0].Width;
		LONG srcHeight = srcResource->m_fixedData.pSurfList[0].Height;
		data.SrcRect = { 0, 0, srcWidth, srcHeight };
		data.DstRect = g_presentationRect;

		auto& repo = SurfaceRepository::get(m_device.getAdapter());
		const auto& rtSurface = repo.getTempRenderTarget(srcWidth, srcHeight);
		auto rt = rtSurface.resource ? rtSurface.resource : this;
		auto rtIndex = rtSurface.resource ? 0 : data.DstSubResourceIndex;
		auto rtRect = rtSurface.resource ? data.SrcRect : data.DstRect;

		if (D3DDDIPOOL_SYSTEMMEM == srcResource->m_fixedData.Pool)
		{
			srcResource = repo.getTempTexture(srcWidth, srcHeight, getPixelFormat(srcResource->m_fixedData.Format)).resource;
			if (!srcResource)
			{
				return E_OUTOFMEMORY;
			}
			copySubResourceRegion(*srcResource, 0, data.SrcRect, data.hSrcResource, data.SrcSubResourceIndex, data.SrcRect);
		}

		if (D3DDDIFMT_P8 == srcResource->m_origData.Format)
		{
			auto entries(Gdi::Palette::getHardwarePalette());
			RGBQUAD pal[256] = {};
			for (UINT i = 0; i < 256; ++i)
			{
				pal[i].rgbRed = entries[i].peRed;
				pal[i].rgbGreen = entries[i].peGreen;
				pal[i].rgbBlue = entries[i].peBlue;
			}
			m_device.getShaderBlitter().palettizedBlt(*rt, rtIndex, rtRect, *srcResource, data.SrcRect, pal);
		}
		else
		{
			copySubResourceRegion(*rt, rtIndex, rtRect, *srcResource, 0, data.SrcRect);
		}

		presentLayeredWindows(*rt, rtIndex, rtRect);

		const auto cursorInfo = Gdi::Cursor::getEmulatedCursorInfo();
		const bool isCursorEmulated = cursorInfo.flags == CURSOR_SHOWING && cursorInfo.hCursor;
		if (isCursorEmulated)
		{
			m_device.getShaderBlitter().cursorBlt(*rt, rtIndex, rtRect, cursorInfo.hCursor, cursorInfo.ptScreenPos);
		}

		if (!rtSurface.resource)
		{
			return S_OK;
		}

		const LONG dstWidth = data.DstRect.right - data.DstRect.left;
		const LONG dstHeight = data.DstRect.bottom - data.DstRect.top;
		while (downscale(rt, data.SrcRect.right, data.SrcRect.bottom, dstWidth, dstHeight))
		{
		}

		const SurfaceRepository::Surface* rtGamma = nullptr;
		if (!ShaderBlitter::isGammaRampDefault() &&
			SurfaceRepository::get(m_device.getAdapter()).getGammaRampTexture())
		{
			rtGamma = &getNextRenderTarget(rt, dstWidth, dstHeight);
		}
		const bool useGamma = rtGamma && rtGamma->resource;
		auto& rtNext = useGamma ? *rtGamma->resource : *this;
		auto rtNextIndex = useGamma ? 0 : data.DstSubResourceIndex;
		auto rtNextRect = useGamma ? RECT{ 0, 0, dstWidth, dstHeight } : data.DstRect;

		if (Config::Settings::DisplayFilter::BILINEAR == Config::displayFilter.get())
		{
			m_device.getShaderBlitter().genBilinearBlt(rtNext, rtNextIndex, rtNextRect,
				*rt, data.SrcRect, Config::displayFilter.getParam());
		}
		else
		{
			D3DDDIARG_BLT blt = {};
			blt.hSrcResource = *rt;
			blt.SrcSubResourceIndex = 0;
			blt.SrcRect = data.SrcRect;
			blt.hDstResource = rtNext;
			blt.DstSubResourceIndex = rtNextIndex;
			blt.DstRect = rtNextRect;
			blt.Flags.Point = 1;
			m_device.getOrigVtable().pfnBlt(m_device, &blt);
		}

		if (useGamma)
		{
			m_device.getShaderBlitter().gammaBlt(*this, data.DstSubResourceIndex, data.DstRect, rtNext, rtNextRect);
		}
		return S_OK;
	}

	void Resource::presentLayeredWindows(Resource& dst, UINT dstSubResourceIndex, const RECT& dstRect)
	{
		auto& blitter = m_device.getShaderBlitter();
		auto& repo = SurfaceRepository::get(m_device.getAdapter());
		RECT monitorRect = DDraw::PrimarySurface::getMonitorRect();
		auto layeredWindows(Gdi::Window::getVisibleLayeredWindows());

		for (auto& layeredWindow : layeredWindows)
		{
			RECT visibleRect = {};
			IntersectRect(&visibleRect, &layeredWindow.rect, &monitorRect);
			if (IsRectEmpty(&visibleRect))
			{
				continue;
			}

			RECT srcRect = { 0, 0, visibleRect.right - visibleRect.left, visibleRect.bottom - visibleRect.top };
			auto& windowSurface = repo.getTempSysMemSurface(srcRect.right, srcRect.bottom);
			auto& texture = repo.getTempTexture(srcRect.right, srcRect.bottom, getPixelFormat(D3DDDIFMT_A8R8G8B8));
			if (!windowSurface.resource || !texture.resource)
			{
				continue;
			}

			HDC srcDc = GetWindowDC(layeredWindow.hwnd);
			HDC dstDc = nullptr;
			windowSurface.surface->GetDC(windowSurface.surface, &dstDc);
			CALL_ORIG_FUNC(BitBlt)(dstDc, 0, 0, srcRect.right, srcRect.bottom, srcDc,
				visibleRect.left - layeredWindow.rect.left, visibleRect.top - layeredWindow.rect.top, SRCCOPY);
			windowSurface.surface->ReleaseDC(windowSurface.surface, dstDc);
			ReleaseDC(layeredWindow.hwnd, srcDc);

			copySubResourceRegion(*texture.resource, 0, srcRect, *windowSurface.resource, 0, srcRect);
			texture.resource->notifyLock(0);

			COLORREF colorKey = 0;
			BYTE alpha = 0;
			DWORD flags = 0;
			GetLayeredWindowAttributes(layeredWindow.hwnd, &colorKey, &alpha, &flags);
			if (flags & ULW_COLORKEY)
			{
				colorKey = ((colorKey & 0xFF) << 16) | (colorKey & 0xFF00) | ((colorKey & 0xFF0000) >> 16);
			}

			if (layeredWindow.region)
			{
				layeredWindow.region &= monitorRect;
				layeredWindow.region.offset(-visibleRect.left, -visibleRect.top);
			}
			Rect::transform(visibleRect, monitorRect, dstRect);

			blitter.textureBlt(dst, dstSubResourceIndex, visibleRect, *texture.resource, 0, srcRect, D3DTEXF_POINT,
				(flags & ULW_COLORKEY) ? reinterpret_cast<UINT*>(&colorKey) : nullptr,
				(flags & ULW_ALPHA) ? &alpha : nullptr,
				layeredWindow.region);
		}
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

	HRESULT Resource::shaderBlt(D3DDDIARG_BLT& data, Resource& srcResource)
	{
		LOG_FUNC("Resource::shaderBlt", data, srcResource);
		auto& repo = SurfaceRepository::get(m_device.getAdapter());

		Resource* srcRes = &srcResource.prepareForBltSrc(data);
		UINT srcIndex = data.SrcSubResourceIndex;
		RECT srcRect = data.SrcRect;

		Resource* dstRes = &prepareForBltDst(data);
		UINT dstIndex = data.DstSubResourceIndex;
		RECT dstRect = data.DstRect;

		if (!srcResource.m_fixedData.Flags.Texture || D3DDDIPOOL_SYSTEMMEM == srcResource.m_fixedData.Pool)
		{
			DWORD width = data.SrcRect.right - data.SrcRect.left;
			DWORD height = data.SrcRect.bottom - data.SrcRect.top;
			auto& texture = repo.getTempTexture(width, height, getPixelFormat(srcResource.m_fixedData.Format));
			if (!texture.resource)
			{
				return LOG_RESULT(E_OUTOFMEMORY);
			}

			srcRes = texture.resource;
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

		if (!m_fixedData.Flags.RenderTarget)
		{
			LONG width = data.DstRect.right - data.DstRect.left;
			LONG height = data.DstRect.bottom - data.DstRect.top;
			auto& rt = repo.getTempRenderTarget(width, height);
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

		m_device.getShaderBlitter().textureBlt(*dstRes, dstIndex, dstRect, *srcRes, srcIndex, srcRect,
			D3DTEXF_POINT, data.Flags.SrcColorKey ? &data.ColorKey : nullptr);

		if (!m_fixedData.Flags.RenderTarget)
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

	HRESULT Resource::unlock(const D3DDDIARG_UNLOCK& data)
	{
		return (m_lockResource || m_isOversized) ? S_OK : m_device.getOrigVtable().pfnUnlock(m_device, &data);
	}

	void Resource::updateConfig()
	{
		if (m_isSurfaceRepoResource)
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
		m_multiSampleConfig = msaa;
		m_formatConfig = formatConfig;
		m_scaledSize = scaledSize;

		if (m_fixedData.Flags.RenderTarget &&
			(m_msaaSurface.resource || m_msaaResolvedSurface.resource))
		{
			for (UINT i = 0; i < m_lockData.size(); ++i)
			{
				if (m_lockData[i].isMsaaUpToDate || m_lockData[i].isMsaaResolvedUpToDate)
				{
					loadVidMemResource(i);
				}
				m_lockData[i].isMsaaUpToDate = false;
				m_lockData[i].isMsaaResolvedUpToDate = false;
			}
		}

		m_msaaSurface = {};
		m_msaaResolvedSurface = {};

		if (D3DDDIMULTISAMPLE_NONE != msaa.first || m_fixedData.Format != formatConfig ||
			static_cast<LONG>(m_fixedData.pSurfList[0].Width) != m_scaledSize.cx ||
			static_cast<LONG>(m_fixedData.pSurfList[0].Height) != m_scaledSize.cy)
		{
			if (m_fixedData.Flags.ZBuffer)
			{
				DDPIXELFORMAT pf = {};
				pf.dwSize = sizeof(pf);
				pf.dwFlags = DDPF_ZBUFFER;
				pf.dwZBufferBitDepth = 16;
				pf.dwZBitMask = 0xFFFF;

				g_formatOverride = formatConfig;
				g_msaaOverride = msaa;
				SurfaceRepository::get(m_device.getAdapter()).getSurface(m_msaaSurface,
					scaledSize.cx, scaledSize.cy, pf,
					DDSCAPS_ZBUFFER | DDSCAPS_VIDEOMEMORY, m_fixedData.SurfCount);
				g_formatOverride = D3DDDIFMT_UNKNOWN;
				g_msaaOverride = {};
			}
			else
			{
				if (D3DDDIMULTISAMPLE_NONE != msaa.first)
				{
					g_msaaOverride = msaa;
					SurfaceRepository::get(m_device.getAdapter()).getSurface(m_msaaSurface,
						scaledSize.cx, scaledSize.cy, getPixelFormat(formatConfig),
						DDSCAPS_3DDEVICE | DDSCAPS_VIDEOMEMORY, m_fixedData.SurfCount);
					g_msaaOverride = {};
				}

				SurfaceRepository::get(m_device.getAdapter()).getSurface(m_msaaResolvedSurface,
					scaledSize.cx, scaledSize.cy, getPixelFormat(formatConfig),
					DDSCAPS_3DDEVICE | DDSCAPS_VIDEOMEMORY, m_fixedData.SurfCount);
				if (!m_msaaResolvedSurface.resource && m_msaaSurface.resource)
				{
					m_msaaSurface = {};
					SurfaceRepository::get(m_device.getAdapter()).getSurface(m_msaaResolvedSurface,
						scaledSize.cx, scaledSize.cy, getPixelFormat(formatConfig),
						DDSCAPS_3DDEVICE | DDSCAPS_VIDEOMEMORY, m_fixedData.SurfCount);
				}
			}
		}
	}
}
