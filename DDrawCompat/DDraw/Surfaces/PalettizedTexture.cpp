#include <memory>

#include <Common/Log.h>
#include <D3dDdi/Device.h>
#include <D3dDdi/FormatInfo.h>
#include <D3dDdi/Resource.h>
#include <DDraw/DirectDrawSurface.h>
#include <DDraw/Surfaces/PalettizedTexture.h>
#include <DDraw/Surfaces/PalettizedTextureImpl.h>

namespace DDraw
{
	template <typename TDirectDraw, typename TSurface, typename TSurfaceDesc>
	HRESULT PalettizedTexture::create(CompatRef<TDirectDraw> dd, TSurfaceDesc desc, TSurface*& surface)
	{
		LOG_FUNC("PalettizedTexture::create", &dd, desc, surface);

		DDSURFACEDESC d = {};
		memcpy(&d, &desc, sizeof(d));
		d.dwSize = sizeof(d);

		auto dd1(CompatPtr<IDirectDraw>::from(&dd));
		CompatPtr<IDirectDrawSurface> palettizedSurface;
		HRESULT result = Surface::create<IDirectDraw>(*dd1, d, palettizedSurface.getRef(),
			std::make_unique<DDraw::Surface>(desc.dwFlags, desc.ddsCaps.dwCaps));
		if (FAILED(result))
		{
			return LOG_RESULT(result);
		}

		desc.dwFlags = DDSD_WIDTH | DDSD_HEIGHT | DDSD_PIXELFORMAT | DDSD_CAPS |
			(desc.dwFlags & (DDSD_CKSRCBLT | DDSD_CKDESTBLT));
		desc.ddpfPixelFormat = D3dDdi::getPixelFormat(D3DDDIFMT_A8R8G8B8);
		desc.ddsCaps = {};
		desc.ddsCaps.dwCaps = DDSCAPS_TEXTURE | DDSCAPS_3DDEVICE | DDSCAPS_VIDEOMEMORY;

		auto privateData(std::make_unique<PalettizedTexture>(desc.dwFlags, desc.ddsCaps.dwCaps));
		auto data = privateData.get();
		data->m_palettizedSurface = palettizedSurface;

		result = Surface::create(dd, desc, surface, std::move(privateData));
		if (FAILED(result))
		{
			return LOG_RESULT(result);
		}

		auto palettizedResource = D3dDdi::Device::findResource(
			DDraw::DirectDrawSurface::getDriverResourceHandle(*data->m_palettizedSurface));
		auto paletteResolvedResource = D3dDdi::Device::findResource(
			DDraw::DirectDrawSurface::getDriverResourceHandle(*surface));
		paletteResolvedResource->setPalettizedTexture(*palettizedResource);

		return LOG_RESULT(DD_OK);
	}

	template HRESULT PalettizedTexture::create(
		CompatRef<IDirectDraw> dd, DDSURFACEDESC desc, IDirectDrawSurface*& surface);
	template HRESULT PalettizedTexture::create(
		CompatRef<IDirectDraw2> dd, DDSURFACEDESC desc, IDirectDrawSurface*& surface);
	template HRESULT PalettizedTexture::create(
		CompatRef<IDirectDraw4> dd, DDSURFACEDESC2 desc, IDirectDrawSurface4*& surface);
	template HRESULT PalettizedTexture::create(
		CompatRef<IDirectDraw7> dd, DDSURFACEDESC2 desc, IDirectDrawSurface7*& surface);

	PalettizedTexture::~PalettizedTexture()
	{
		LOG_FUNC("PalettizedTexture::~PalettizedTexture", this);
	}

	void PalettizedTexture::createImpl()
	{
		m_impl.reset(new PalettizedTextureImpl<IDirectDrawSurface>(*this, m_palettizedSurface));
		m_impl2.reset(new PalettizedTextureImpl<IDirectDrawSurface2>(*this, m_palettizedSurface));
		m_impl3.reset(new PalettizedTextureImpl<IDirectDrawSurface3>(*this, m_palettizedSurface));
		m_impl4.reset(new PalettizedTextureImpl<IDirectDrawSurface4>(*this, m_palettizedSurface));
		m_impl7.reset(new PalettizedTextureImpl<IDirectDrawSurface7>(*this, m_palettizedSurface));
	}
}
