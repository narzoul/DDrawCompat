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

namespace
{
	std::map<LUID, D3dDdi::SurfaceRepository> g_repositories;
}

namespace D3dDdi
{
	SurfaceRepository::SurfaceRepository(const Adapter& adapter)
		: m_adapter(adapter)
		, m_cursor(nullptr)
		, m_cursorSize{}
		, m_cursorHotspot{}
	{
	}

	CompatPtr<IDirectDrawSurface7> SurfaceRepository::createSurface(
		DWORD width, DWORD height, const DDPIXELFORMAT& pf, DWORD caps, UINT surfaceCount)
	{
		auto dd(m_adapter.getRepository());
		if (!dd)
		{
			LOG_ONCE("ERROR: no DirectDraw repository available");
			return nullptr;
		}

		m_releasedSurfaces.clear();
		CompatPtr<IDirectDrawSurface7> surface;

		DDSURFACEDESC2 desc = {};
		desc.dwSize = sizeof(desc);
		desc.dwFlags = DDSD_WIDTH | DDSD_HEIGHT | DDSD_PIXELFORMAT | DDSD_CAPS;
		desc.dwWidth = width;
		desc.dwHeight = height;
		desc.ddpfPixelFormat = pf;
		desc.ddsCaps.dwCaps = caps;
		if (surfaceCount > 1)
		{
			desc.dwFlags |= DDSD_BACKBUFFERCOUNT;
			desc.ddsCaps.dwCaps |= DDSCAPS_COMPLEX | DDSCAPS_FLIP;
			desc.dwBackBufferCount = surfaceCount - 1;
		}

		s_inCreateSurface = true;
		HRESULT result = dd->CreateSurface(dd, &desc, &surface.getRef(), nullptr);
		s_inCreateSurface = false;
		if (FAILED(result))
		{
			LOG_ONCE("ERROR: Failed to create repository surface: " << Compat::hex(result) << " " << desc);
			return nullptr;
		}
		return surface;
	}

	SurfaceRepository& SurfaceRepository::get(const Adapter& adapter)
	{
		auto it = g_repositories.find(adapter.getLuid());
		if (it != g_repositories.end())
		{
			return it->second;
		}
		return g_repositories.emplace(adapter.getLuid(), SurfaceRepository(adapter)).first->second;
	}

	Resource* SurfaceRepository::getBitmapResource(
		Surface& surface, HBITMAP bitmap, const RECT& rect, const DDPIXELFORMAT& pf, DWORD caps)
	{
		DWORD width = rect.right - rect.left;
		DWORD height = rect.bottom - rect.top;
		auto resource = getSurface(surface, width, height, pf, caps).resource;
		if (!resource)
		{
			return nullptr;
		}

		HDC srcDc = CreateCompatibleDC(nullptr);
		HGDIOBJ prevBitmap = SelectObject(srcDc, bitmap);
		HDC dstDc = nullptr;
		PALETTEENTRY palette[256] = {};
		palette[255] = { 0xFF, 0xFF, 0xFF };
		KernelModeThunks::setDcPaletteOverride(palette);
		surface.surface->GetDC(surface.surface, &dstDc);
		KernelModeThunks::setDcPaletteOverride(nullptr);

		CALL_ORIG_FUNC(BitBlt)(dstDc, 0, 0, width, height, srcDc, rect.left, rect.top, SRCCOPY);

		surface.surface->ReleaseDC(surface.surface, dstDc);
		SelectObject(srcDc, prevBitmap);
		DeleteDC(srcDc);
		return resource;
	}

	SurfaceRepository::Cursor SurfaceRepository::getCursor(HCURSOR cursor)
	{
		if (isLost(m_cursorMaskTexture) || isLost(m_cursorColorTexture))
		{
			m_cursor = nullptr;
			m_cursorMaskTexture = {};
			m_cursorColorTexture = {};
		}

		if (cursor != m_cursor)
		{
			m_cursor = cursor;
			ICONINFO iconInfo = {};
			if (!GetIconInfo(cursor, &iconInfo))
			{
				return {};
			}

			BITMAP bm = {};
			GetObject(iconInfo.hbmMask, sizeof(bm), &bm);

			RECT rect = {};
			SetRect(&rect, 0, 0, bm.bmWidth, bm.bmHeight);
			if (!iconInfo.hbmColor)
			{
				rect.bottom /= 2;
			}

			getBitmapResource(m_cursorMaskTexture, iconInfo.hbmMask, rect,
				DDraw::DirectDraw::getRgbPixelFormat(8), DDSCAPS_OFFSCREENPLAIN | DDSCAPS_VIDEOMEMORY);

			if (iconInfo.hbmColor)
			{
				getBitmapResource(m_cursorColorTexture, iconInfo.hbmColor, rect,
					DDraw::DirectDraw::getRgbPixelFormat(32), DDSCAPS_TEXTURE | DDSCAPS_VIDEOMEMORY);
				DeleteObject(iconInfo.hbmColor);
			}
			else
			{
				OffsetRect(&rect, 0, rect.bottom);
				getBitmapResource(m_cursorColorTexture, iconInfo.hbmMask, rect,
					DDraw::DirectDraw::getRgbPixelFormat(8), DDSCAPS_OFFSCREENPLAIN | DDSCAPS_VIDEOMEMORY);
			}
			DeleteObject(iconInfo.hbmMask);

			m_cursorMaskTexture.resource->prepareForGpuRead(0);
			m_cursorColorTexture.resource->prepareForGpuRead(0);

			m_cursorSize.cx = rect.right - rect.left;
			m_cursorSize.cy = rect.bottom - rect.top;
			m_cursorHotspot.x = iconInfo.xHotspot;
			m_cursorHotspot.y = iconInfo.yHotspot;
		}

		Cursor result = {};
		result.cursor = m_cursor;
		result.size = m_cursorSize;
		result.hotspot = m_cursorHotspot;
		result.maskTexture = m_cursorMaskTexture.resource;
		result.colorTexture = m_cursorColorTexture.resource;
		result.tempTexture = getSurface(m_cursorTempTexture, m_cursorSize.cx, m_cursorSize.cy,
			DDraw::DirectDraw::getRgbPixelFormat(32), DDSCAPS_TEXTURE | DDSCAPS_VIDEOMEMORY).resource;
		return result;
	}

	Resource* SurfaceRepository::getLogicalXorTexture()
	{
		return getInitializedResource(m_logicalXorTexture, 256, 256, DDraw::DirectDraw::getRgbPixelFormat(8),
			DDSCAPS_OFFSCREENPLAIN | DDSCAPS_VIDEOMEMORY,
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
		const DDPIXELFORMAT& pf, DWORD caps, std::function<void(const DDSURFACEDESC2&)> initFunc)
	{
		if (!isLost(surface) || !getSurface(surface, width, height, pf, caps).resource)
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
		return getSurface(m_paletteTexture, 256, 1, DDraw::DirectDraw::getRgbPixelFormat(32),
			DDSCAPS_TEXTURE | DDSCAPS_VIDEOMEMORY).resource;
	}

	SurfaceRepository::Surface& SurfaceRepository::getSurface(Surface& surface, DWORD width, DWORD height,
		const DDPIXELFORMAT& pf, DWORD caps, UINT surfaceCount)
	{
		if (surface.surface && (surface.width != width || surface.height != height ||
			0 != memcmp(&surface.pixelFormat, &pf, sizeof(pf)) || isLost(surface)))
		{
			surface = {};
		}

		if (!surface.surface)
		{
			surface.surface = createSurface(width, height, pf, caps, surfaceCount);
			if (surface.surface)
			{
				surface.resource = D3dDdi::Device::findResource(
					DDraw::DirectDrawSurface::getDriverResourceHandle(*surface.surface));
				surface.width = width;
				surface.height = height;
				surface.pixelFormat = pf;
			}
		}

		return surface;
	}

	const SurfaceRepository::Surface& SurfaceRepository::getTempRenderTarget(DWORD width, DWORD height)
	{
		return getTempSurface(m_renderTarget, width, height, DDraw::DirectDraw::getRgbPixelFormat(32),
			DDSCAPS_3DDEVICE | DDSCAPS_TEXTURE | DDSCAPS_VIDEOMEMORY);
	}

	SurfaceRepository::Surface& SurfaceRepository::getTempSurface(Surface& surface, DWORD width, DWORD height,
		const DDPIXELFORMAT& pf, DWORD caps, UINT surfaceCount)
	{
		return getSurface(surface, max(width, surface.width), max(height, surface.height), pf, caps, surfaceCount);
	}

	const SurfaceRepository::Surface& SurfaceRepository::getTempTexture(DWORD width, DWORD height, const DDPIXELFORMAT& pf)
	{
		return getTempSurface(m_textures[pf], width, height, pf, DDSCAPS_TEXTURE | DDSCAPS_VIDEOMEMORY);
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
