#include <set>
#include <string>

#include <d3d.h>
#include <d3dumddi.h>

#include <Common/CompatPtr.h>
#include <D3dDdi/FormatInfo.h>
#include <D3dDdi/Log/CommonLog.h>
#include <DDraw/DirectDraw.h>
#include <DDraw/LogUsedResourceFormat.h>
#include <Win32/DisplayMode.h>

namespace
{
	struct ResourceFormat
	{
		D3DDDIFORMAT format;
		DWORD caps;
		DWORD caps2;
		DWORD memCaps;
	};

	UINT g_suppressResourceFormatLogs = 0;

	auto toTuple(const ResourceFormat& format)
	{
		return std::make_tuple(format.format, format.caps, format.caps2, format.memCaps);
	}

	bool operator<(const ResourceFormat& left, const ResourceFormat& right)
	{
		return toTuple(left) < toTuple(right);
	}
}

namespace DDraw
{
	LogUsedResourceFormat::LogUsedResourceFormat(const DDSURFACEDESC2& desc, IUnknown*& surface)
		: m_desc(desc)
		, m_surface(surface)
	{
		if (!(m_desc.dwFlags & DDSD_PIXELFORMAT))
		{
			m_desc.ddpfPixelFormat = {};
			if (m_desc.dwFlags & DDSD_ZBUFFERBITDEPTH)
			{
				switch (m_desc.dwMipMapCount)
				{
				case 16: m_desc.ddpfPixelFormat = D3dDdi::getPixelFormat(D3DDDIFMT_D16); break;
				case 24: m_desc.ddpfPixelFormat = D3dDdi::getPixelFormat(D3DDDIFMT_X8D24); break;
				case 32: m_desc.ddpfPixelFormat = D3dDdi::getPixelFormat(D3DDDIFMT_D32); break;
				}
			}
			else if (!(desc.ddsCaps.dwCaps & DDSCAPS_RESERVED2))
			{
				m_desc.ddpfPixelFormat = DDraw::DirectDraw::getRgbPixelFormat(Win32::DisplayMode::getBpp());
			}
		}
	}

	LogUsedResourceFormat::~LogUsedResourceFormat()
	{
		if (g_suppressResourceFormatLogs)
		{
			return;
		}

		auto format = (m_desc.ddsCaps.dwCaps & DDSCAPS_RESERVED2)
			? D3DDDIFMT_VERTEXDATA
			: D3dDdi::getFormat(m_desc.ddpfPixelFormat);
		if (D3DDDIFMT_UNKNOWN == format)
		{
			LOG_ONCE("Unknown resource format: " << format);
		}

		bool isMipMap = (m_desc.ddsCaps.dwCaps & DDSCAPS_MIPMAP) && (m_desc.ddsCaps.dwCaps & DDSCAPS_COMPLEX) &&
			(!(m_desc.dwFlags & DDSD_MIPMAPCOUNT) || m_desc.dwMipMapCount > 1);
		auto caps = m_desc.ddsCaps.dwCaps & (DDSCAPS_SYSTEMMEMORY | DDSCAPS_VIDEOMEMORY |
			DDSCAPS_3DDEVICE | DDSCAPS_TEXTURE | (isMipMap ? DDSCAPS_MIPMAP : 0) | DDSCAPS_ZBUFFER | DDSCAPS_RESERVED2 |
			DDSCAPS_PRIMARYSURFACE | DDSCAPS_OVERLAY);
		auto caps2 = m_desc.ddsCaps.dwCaps2 & (DDSCAPS2_CUBEMAP | DDSCAPS2_TEXTUREMANAGE | DDSCAPS2_D3DTEXTUREMANAGE);
		DWORD memCaps = 0;

		if (!(m_desc.ddsCaps.dwCaps & (DDSCAPS_SYSTEMMEMORY | DDSCAPS_VIDEOMEMORY)) &&
			!(m_desc.ddsCaps.dwCaps2 & (DDSCAPS2_TEXTUREMANAGE | DDSCAPS2_D3DTEXTUREMANAGE)))
		{
			auto surface(CompatPtr<IDirectDrawSurface7>::from(m_surface));
			if (surface)
			{
				DDSCAPS2 realCaps = {};
				surface->GetCaps(surface, &realCaps);
				memCaps = realCaps.dwCaps & (DDSCAPS_SYSTEMMEMORY | DDSCAPS_VIDEOMEMORY);
			}
		}

		static std::set<ResourceFormat> formats;
		if (formats.insert({ format, caps, caps2, memCaps }).second)
		{
			Compat::Log log(Config::Settings::LogLevel::INFO);
			log << "Using resource format: " << format << ',';

			if (caps & DDSCAPS_3DDEVICE)
			{
				log << " render target";
			}
			if (caps & DDSCAPS_ZBUFFER)
			{
				log << " depth buffer";
			}
			if (caps & DDSCAPS_RESERVED2)
			{
				log << " vertex buffer";
			}
			if (caps2 & DDSCAPS2_CUBEMAP)
			{
				log << " cubic";
			}
			if (caps & DDSCAPS_MIPMAP)
			{
				log << " mipmap";
			}
			if (caps & DDSCAPS_TEXTURE)
			{
				log << " texture";
			}
			if (caps & DDSCAPS_PRIMARYSURFACE)
			{
				log << " primary";
			}
			if (caps & DDSCAPS_OVERLAY)
			{
				log << " overlay";
			}
			if (!(caps & (DDSCAPS_3DDEVICE | DDSCAPS_ZBUFFER | DDSCAPS_RESERVED2 | DDSCAPS_TEXTURE |
				DDSCAPS_PRIMARYSURFACE | DDSCAPS_OVERLAY)))
			{
				log << " plain";
			}

			log << ", ";
			if (caps2 & DDSCAPS2_D3DTEXTUREMANAGE)
			{
				log << "D3D managed";
			}
			else if (caps2 & DDSCAPS2_TEXTUREMANAGE)
			{
				log << "managed";
			}
			else if (caps & DDSCAPS_VIDEOMEMORY)
			{
				log << "vidmem";
			}
			else if (caps & DDSCAPS_SYSTEMMEMORY)
			{
				log << "sysmem";
			}
			else
			{
				log << "anymem";
				if (m_surface)
				{
					log << " -> " << ((memCaps & DDSCAPS_VIDEOMEMORY) ? "vidmem" : "sysmem");
				}
			}

			if (!m_surface)
			{
				log << " (FAILED)";
			}
		}
	}

	SuppressResourceFormatLogs::SuppressResourceFormatLogs()
	{
		++g_suppressResourceFormatLogs;
	}

	SuppressResourceFormatLogs::~SuppressResourceFormatLogs()
	{
		--g_suppressResourceFormatLogs;
	}
}
