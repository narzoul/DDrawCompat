#undef WIN32_LEAN_AND_MEAN

#include <map>

#include <Windows.h>
#include <d3dkmthk.h>

#include <Common/Comparison.h>
#include <D3dDdi/Adapter.h>
#include <D3dDdi/Device.h>
#include <D3dDdi/KernelModeThunks.h>
#include <D3dDdi/Resource.h>
#include <D3dDdi/SurfaceRepository.h>
#include <DDraw/DirectDrawSurface.h>
#include <DDraw/LogUsedResourceFormat.h>
#include <Gdi/VirtualScreen.h>

namespace
{
	std::map<LUID, D3dDdi::SurfaceRepository> g_repositories;
	D3dDdi::SurfaceRepository* g_primaryRepository = nullptr;
	bool g_enableSurfaceCheck = true;

	void initDitherTexture(BYTE* tex, DWORD pitch, DWORD x, DWORD y, DWORD size, DWORD mul, DWORD value)
	{
		if (1 == size)
		{
			tex[y * pitch + x] = static_cast<BYTE>(value);
		}
		else
		{
			size /= 2;
			initDitherTexture(tex, pitch, x, y, size, 4 * mul, value + 0 * mul);
			initDitherTexture(tex, pitch, x + size, y, size, 4 * mul, value + 2 * mul);
			initDitherTexture(tex, pitch, x, y + size, size, 4 * mul, value + 3 * mul);
			initDitherTexture(tex, pitch, x + size, y + size, size, 4 * mul, value + 1 * mul);
		}
	}

	void initDitherTexture(BYTE* tex, DWORD pitch, DWORD size)
	{
		initDitherTexture(tex, pitch, 0, 0, size, 1, 0);
	}
}

namespace D3dDdi
{
	SurfaceRepository::SurfaceRepository()
		: m_cursor(nullptr)
		, m_cursorSize{}
		, m_cursorHotspot{}
	{
	}

	CompatPtr<IDirectDrawSurface7> SurfaceRepository::createSurface(
		DWORD width, DWORD height, D3DDDIFORMAT format, DWORD caps, DWORD caps2, UINT surfaceCount)
	{
		if (!m_dd)
		{
			LOG_ONCE("ERROR: no DirectDraw repository available");
			return nullptr;
		}

		m_releasedSurfaces.clear();
		CompatPtr<IDirectDrawSurface7> surface;

		if (D3DDDIFMT_P8 == format)
		{
			format = D3DDDIFMT_L8;
		}

		DDSURFACEDESC2 desc = {};
		desc.dwSize = sizeof(desc);
		desc.dwFlags = DDSD_WIDTH | DDSD_HEIGHT | DDSD_PIXELFORMAT | DDSD_CAPS;
		desc.dwWidth = width;
		desc.dwHeight = height;
		desc.ddpfPixelFormat = getPixelFormat(format);
		desc.ddsCaps.dwCaps = caps;
		desc.ddsCaps.dwCaps2 = caps2;

		if (caps2 & DDSCAPS2_CUBEMAP)
		{
			desc.ddsCaps.dwCaps |= DDSCAPS_COMPLEX;
			surfaceCount /= 6;
		}

		if (caps & DDSCAPS_MIPMAP)
		{
			desc.dwFlags |= DDSD_MIPMAPCOUNT;
			desc.ddsCaps.dwCaps |= DDSCAPS_COMPLEX;
			desc.dwMipMapCount = surfaceCount;
		}
		else if (surfaceCount > 1)
		{
			desc.dwFlags |= DDSD_BACKBUFFERCOUNT;
			desc.ddsCaps.dwCaps |= DDSCAPS_COMPLEX | DDSCAPS_FLIP;
			desc.dwBackBufferCount = surfaceCount - 1;
		}

		if (0 == desc.ddpfPixelFormat.dwFlags || D3dDdi::FOURCC_INTZ == format || D3dDdi::FOURCC_DF16 == format)
		{
			desc.ddpfPixelFormat = getPixelFormat((caps & DDSCAPS_ZBUFFER) ? D3DDDIFMT_D16 : D3DDDIFMT_X8R8G8B8);
			D3dDdi::Resource::setFormatOverride(format);
		}

		DDraw::SuppressResourceFormatLogs suppressResourceFormatLogs;
		s_inCreateSurface = true;
		HRESULT result = m_dd.get()->lpVtbl->CreateSurface(m_dd, &desc, &surface.getRef(), nullptr);
		s_inCreateSurface = false;
		D3dDdi::Resource::setFormatOverride(D3DDDIFMT_UNKNOWN);
		if (FAILED(result))
		{
			LOG_ONCE("ERROR: Failed to create repository surface: " << Compat::hex(result) << " " << desc);
			return nullptr;
		}
		return surface;
	}

	void SurfaceRepository::enableSurfaceCheck(bool enable)
	{
		g_enableSurfaceCheck = enable;
	}

	SurfaceRepository& SurfaceRepository::get(const Adapter& adapter)
	{
		return g_repositories[adapter.getLuid()];
	}

	SurfaceRepository::Cursor SurfaceRepository::getCursor(HCURSOR cursor)
	{
		if (m_cursorMaskTexture.resource && isLost(m_cursorMaskTexture) ||
			m_cursorColorTexture.resource && isLost(m_cursorColorTexture))
		{
			m_cursor = nullptr;
			m_cursorMaskTexture = {};
			m_cursorColorTexture = {};
		}

		Cursor result = {};
		if (cursor != m_cursor)
		{
			m_cursor = cursor;

			std::unique_ptr<HICON__, decltype(&DestroyCursor)> resizedCursor(reinterpret_cast<HCURSOR>(
				CopyImage(cursor, IMAGE_CURSOR, 32, 32, LR_COPYRETURNORG | LR_COPYFROMRESOURCE)), DestroyCursor);
			if (resizedCursor)
			{
				if (resizedCursor.get() == cursor)
				{
					resizedCursor.release();
				}
				else
				{
					cursor = resizedCursor.get();
				}
			}

			ICONINFO iconInfo = {};
			if (!GetIconInfo(cursor, &iconInfo))
			{
				return {};
			}
			std::unique_ptr<void, decltype(&DeleteObject)> bmColor(iconInfo.hbmColor, DeleteObject);
			std::unique_ptr<void, decltype(&DeleteObject)> bmMask(iconInfo.hbmMask, DeleteObject);
			m_cursorHotspot.x = iconInfo.xHotspot;
			m_cursorHotspot.y = iconInfo.yHotspot;

			BITMAP bm = {};
			GetObject(iconInfo.hbmMask, sizeof(bm), &bm);
			m_cursorSize.cx = bm.bmWidth;
			m_cursorSize.cy = bm.bmHeight;
			if (!iconInfo.hbmColor)
			{
				m_cursorSize.cy /= 2;
			}

			if (!getCursorImage(m_cursorColorTexture, cursor, m_cursorSize.cx, m_cursorSize.cy, DI_IMAGE))
			{
				return {};
			}

			if (hasAlpha(*m_cursorColorTexture.surface))
			{
				m_cursorMaskTexture = {};
			}
			else if (!getCursorImage(m_cursorMaskTexture, cursor, m_cursorSize.cx, m_cursorSize.cy, DI_MASK))
			{
				return {};
			}
		}

		result.cursor = m_cursor;
		result.size = m_cursorSize;
		result.hotspot = m_cursorHotspot;
		result.colorTexture = m_cursorColorTexture.resource;
		if (m_cursorMaskTexture.resource)
		{
			result.maskTexture = m_cursorMaskTexture.resource;
			result.tempTexture = getSurface(m_cursorTempTexture, m_cursorSize.cx, m_cursorSize.cy,
				D3DDDIFMT_X8R8G8B8, DDSCAPS_TEXTURE | DDSCAPS_VIDEOMEMORY).resource;
			if (!result.tempTexture)
			{
				return {};
			}
		}
		return result;
	}

	bool SurfaceRepository::getCursorImage(Surface& surface, HCURSOR cursor, DWORD width, DWORD height, UINT flags)
	{
		if (!getSurface(surface, width, height, D3DDDIFMT_A8R8G8B8, DDSCAPS_TEXTURE | DDSCAPS_VIDEOMEMORY).resource)
		{
			return false;
		}

		HDC dc = nullptr;
		if (FAILED(surface.surface->GetDC(surface.surface, &dc)))
		{
			return false;
		}

		CALL_ORIG_FUNC(DrawIconEx)(dc, 0, 0, cursor, width, height, 0, nullptr, flags);
		surface.surface->ReleaseDC(surface.surface, dc);
		return true;
	}

	Resource* SurfaceRepository::getDitherTexture(DWORD size)
	{
		return getInitializedResource(m_ditherTexture, size, size, D3DDDIFMT_L8, DDSCAPS_TEXTURE | DDSCAPS_VIDEOMEMORY,
			[](const DDSURFACEDESC2& desc) {
				initDitherTexture(static_cast<BYTE*>(desc.lpSurface), desc.lPitch, 16);
			});
	}

	Resource* SurfaceRepository::getGammaRampTexture()
	{
		return getSurface(m_gammaRampTexture, 256, 3, D3DDDIFMT_L8, DDSCAPS_TEXTURE | DDSCAPS_VIDEOMEMORY).resource;
	}

	Resource* SurfaceRepository::getLogicalXorTexture()
	{
		return getInitializedResource(m_logicalXorTexture, 256, 256, D3DDDIFMT_L8, DDSCAPS_TEXTURE | DDSCAPS_VIDEOMEMORY,
			[](const DDSURFACEDESC2& desc) {
				BYTE* p = static_cast<BYTE*>(desc.lpSurface);
				for (UINT y = 0; y < 256; ++y)
				{
					for (UINT x = 0; x < 256; ++x)
					{
						p[x] = static_cast<BYTE>(x ^ y);
					}
					p += desc.lPitch;
				}
			});
	}

	Resource* SurfaceRepository::getInitializedResource(Surface& surface, DWORD width, DWORD height,
		D3DDDIFORMAT format, DWORD caps, std::function<void(const DDSURFACEDESC2&)> initFunc)
	{
		if (!isLost(surface) || !getSurface(surface, width, height, format, caps).resource)
		{
			return surface.resource;
		}

		DDSURFACEDESC2 desc = {};
		desc.dwSize = sizeof(desc);
		surface.surface->Lock(surface.surface, nullptr, &desc, DDLOCK_DISCARDCONTENTS | DDLOCK_WAIT, nullptr);
		if (!desc.lpSurface)
		{
			return nullptr;
		}

		initFunc(desc);
		surface.surface->Unlock(surface.surface, nullptr);
		surface.resource->prepareForGpuRead(0);
		return surface.resource;
	}

	const SurfaceRepository::Surface& SurfaceRepository::getNextRenderTarget(
		DWORD width, DWORD height, D3DDDIFORMAT format, const Resource* currentSrcRt, const Resource* currentDstRt)
	{
		const bool hq = getFormatInfo(format).red.bitCount > 8;
		auto& renderTargets = hq ? m_hqRenderTargets : m_renderTargets;
		std::size_t index = 0;
		while (index < renderTargets.size())
		{
			auto rt = renderTargets[index].resource;
			if (!rt || rt != currentSrcRt && rt != currentDstRt)
			{
				break;
			}
			++index;
		}
		return getTempSurface(renderTargets[index], width, height, hq ? format : D3DDDIFMT_X8R8G8B8,
			DDSCAPS_3DDEVICE | DDSCAPS_TEXTURE | DDSCAPS_VIDEOMEMORY);
	}

	Resource* SurfaceRepository::getPaletteTexture()
	{
		return getSurface(m_paletteTexture, 256, 1, D3DDDIFMT_A8R8G8B8, DDSCAPS_TEXTURE | DDSCAPS_VIDEOMEMORY).resource;
	}

	SurfaceRepository& SurfaceRepository::getPrimaryRepo()
	{
		return *g_primaryRepository;
	}

	SurfaceRepository::Surface& SurfaceRepository::getSurface(Surface& surface, DWORD width, DWORD height,
		D3DDDIFORMAT format, DWORD caps, UINT surfaceCount, DWORD caps2)
	{
		if (!g_enableSurfaceCheck)
		{
			return surface;
		}

		if (surface.width != width || surface.height != height || surface.format != format || isLost(surface))
		{
			surface = {};
		}

		if (!surface.surface)
		{
			surface.surface = createSurface(width, height, format, caps, caps2, surfaceCount);
			if (surface.surface)
			{
				surface.resource = D3dDdi::Device::findResource(
					DDraw::DirectDrawSurface::getDriverResourceHandle(*surface.surface));
				surface.width = width;
				surface.height = height;
				surface.format = format;
			}
		}

		return surface;
	}

	SurfaceRepository::Surface& SurfaceRepository::getSyncSurface(D3DDDIFORMAT format)
	{
		return getSurface(m_syncSurface[format], 16, 16, format, DDSCAPS_OFFSCREENPLAIN | DDSCAPS_VIDEOMEMORY);
	}

	SurfaceRepository::Surface& SurfaceRepository::getTempSurface(Surface& surface, DWORD width, DWORD height,
		D3DDDIFORMAT format, DWORD caps, UINT surfaceCount)
	{
		return getSurface(surface, std::max(width, surface.width), std::max(height, surface.height),
			format, caps, surfaceCount);
	}

	SurfaceRepository::Surface& SurfaceRepository::getTempSysMemSurface(DWORD width, DWORD height)
	{
		return getTempSurface(m_sysMemSurface, width, height, D3DDDIFMT_A8R8G8B8,
			DDSCAPS_OFFSCREENPLAIN | DDSCAPS_SYSTEMMEMORY);
	}

	const SurfaceRepository::Surface& SurfaceRepository::getTempTexture(DWORD width, DWORD height, D3DDDIFORMAT format)
	{
		return getTempSurface(m_textures[format], width, height, format,
			(D3DDDIFMT_P8 == format ? 0 : DDSCAPS_TEXTURE) | DDSCAPS_VIDEOMEMORY);
	}

	CompatPtr<IDirectDrawSurface7> SurfaceRepository::getWindowedBackBuffer(DWORD width, DWORD height)
	{
		s_isLockResourceEnabled = true;
		auto surface = getSurface(m_windowedBackBuffer, width, height, D3DDDIFMT_X8R8G8B8,
			DDSCAPS_OFFSCREENPLAIN | DDSCAPS_3DDEVICE | DDSCAPS_VIDEOMEMORY).surface;
		s_isLockResourceEnabled = false;
		return surface;
	}

	CompatWeakPtr<IDirectDrawSurface7> SurfaceRepository::getWindowedPrimary()
	{
		if (m_windowedPrimary)
		{
			if (SUCCEEDED(m_windowedPrimary->IsLost(m_windowedPrimary)))
			{
				return m_windowedPrimary;
			}
			m_windowedPrimary.release();
		}

		DDSURFACEDESC2 desc = {};
		desc.dwSize = sizeof(desc);
		desc.dwFlags = DDSD_CAPS;
		desc.ddsCaps.dwCaps = DDSCAPS_PRIMARYSURFACE;
		HRESULT result = m_dd->CreateSurface(m_dd, &desc, &m_windowedPrimary.getRef(), nullptr);
		if (FAILED(result))
		{
			LOG_ONCE("ERROR: Failed to create primary surface in repository: " << Compat::hex(result) << " " << desc);
		}

		return m_windowedPrimary;
	}

	CompatPtr<IDirectDrawSurface7> SurfaceRepository::getWindowedSrc(RECT rect)
	{
		CompatPtr<IDirectDrawSurface7> src;
		auto desc = Gdi::VirtualScreen::getSurfaceDesc(rect);
		m_dd->CreateSurface(m_dd, &desc, &src.getRef(), nullptr);
		return src;
	}

	bool SurfaceRepository::hasAlpha(CompatRef<IDirectDrawSurface7> surface)
	{
		DDSURFACEDESC2 desc = {};
		desc.dwSize = sizeof(desc);
		if (FAILED(surface->Lock(&surface, nullptr, &desc, DDLOCK_WAIT, nullptr)))
		{
			return false;
		}

		for (UINT y = 0; y < desc.dwHeight; ++y)
		{
			const DWORD* p = reinterpret_cast<DWORD*>(static_cast<BYTE*>(desc.lpSurface) + y * desc.lPitch);
			for (UINT x = 0; x < desc.dwWidth; ++x)
			{
				if (*p & 0xFF000000)
				{
					surface->Unlock(&surface, nullptr);
					return true;
				}
				++p;
			}
		}

		surface->Unlock(&surface, nullptr);
		return false;
	}

	bool SurfaceRepository::isLost(Surface& surface)
	{
		return !surface.surface || FAILED(surface.surface->IsLost(surface.surface));
	}

	void SurfaceRepository::release(Surface& surface)
	{
		if (surface.surface)
		{
			m_releasedSurfaces.push_back(surface);
			surface = {};
		}
	}

	void SurfaceRepository::setRepository(CompatWeakPtr<IDirectDraw7> dd)
	{
		m_dd = dd;
		if (!g_primaryRepository)
		{
			g_primaryRepository = this;
		}
	}

	bool SurfaceRepository::s_inCreateSurface = false;
	bool SurfaceRepository::s_isLockResourceEnabled = false;
}
