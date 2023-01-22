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

namespace
{
	std::map<LUID, D3dDdi::SurfaceRepository> g_repositories;
	bool g_enableSurfaceCheck = true;
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

		if (0 == desc.ddpfPixelFormat.dwFlags)
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

	Resource* SurfaceRepository::getPaletteTexture()
	{
		return getSurface(m_paletteTexture, 256, 1, D3DDDIFMT_A8R8G8B8, DDSCAPS_TEXTURE | DDSCAPS_VIDEOMEMORY).resource;
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

	const SurfaceRepository::Surface& SurfaceRepository::getTempRenderTarget(DWORD width, DWORD height, UINT index)
	{
		if (index >= m_renderTargets.size())
		{
			m_renderTargets.resize(index + 1);
		}
		return getTempSurface(m_renderTargets[index], width, height, D3DDDIFMT_A8R8G8B8,
			DDSCAPS_3DDEVICE | DDSCAPS_TEXTURE | DDSCAPS_VIDEOMEMORY);
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

	bool SurfaceRepository::s_inCreateSurface = false;
}
