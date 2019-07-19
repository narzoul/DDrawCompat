#include <initguid.h>

#include "Common/CompatPtr.h"
#include "DDraw/DirectDraw.h"
#include "DDraw/DirectDrawSurface.h"
#include "DDraw/Surfaces/Surface.h"
#include "DDraw/Surfaces/SurfaceImpl.h"
#include "Win32/DisplayMode.h"

// {C62D8849-DFAC-4454-A1E8-DA67446426BA}
DEFINE_GUID(IID_CompatSurfacePrivateData,
	0xc62d8849, 0xdfac, 0x4454, 0xa1, 0xe8, 0xda, 0x67, 0x44, 0x64, 0x26, 0xba);

namespace
{
	void fixSurfaceDesc(DWORD& flags, DWORD& caps)
	{
		if ((flags & DDSD_WIDTH) &&
			(flags & DDSD_HEIGHT) &&
			!(caps & (DDSCAPS_ALPHA | DDSCAPS_ZBUFFER)))
		{
			if (!(caps & (DDSCAPS_OFFSCREENPLAIN | DDSCAPS_OVERLAY | DDSCAPS_TEXTURE |
				DDSCAPS_FRONTBUFFER | DDSCAPS_BACKBUFFER)))
			{
				caps |= DDSCAPS_OFFSCREENPLAIN;
			}
		}
	}
}

namespace DDraw
{
	HRESULT STDMETHODCALLTYPE Surface::QueryInterface(REFIID, LPVOID*)
	{
		return E_NOINTERFACE;
	}

	ULONG STDMETHODCALLTYPE Surface::AddRef()
	{
		return ++m_refCount;
	}

	ULONG STDMETHODCALLTYPE Surface::Release()
	{
		DWORD refCount = --m_refCount;
		if (0 == refCount)
		{
			delete this;
		}
		return refCount;
	}

	Surface::Surface()
		: m_ddObject(nullptr)
		, m_refCount(0)
	{
	}

	Surface::~Surface()
	{
	}

	void Surface::attach(CompatRef<IDirectDrawSurface7> dds, std::unique_ptr<Surface> privateData)
	{
		if (SUCCEEDED(dds->SetPrivateData(&dds, IID_CompatSurfacePrivateData,
			privateData.get(), sizeof(privateData.get()), DDSPD_IUNKNOWNPOINTER)))
		{
			CompatPtr<IUnknown> dd;
			dds.get().lpVtbl->GetDDInterface(&dds, reinterpret_cast<void**>(&dd.getRef()));

			privateData->createImpl();
			privateData->m_impl->m_data = privateData.get();
			privateData->m_impl2->m_data = privateData.get();
			privateData->m_impl3->m_data = privateData.get();
			privateData->m_impl4->m_data = privateData.get();
			privateData->m_impl7->m_data = privateData.get();

			privateData->m_ddObject = DDraw::getDdObject(*CompatPtr<IDirectDraw>(dd));

			privateData.release();
		}
	}

	template <typename TDirectDraw, typename TSurface, typename TSurfaceDesc>
	HRESULT Surface::create(
		CompatRef<TDirectDraw> dd, TSurfaceDesc desc, TSurface*& surface, std::unique_ptr<Surface> privateData)
	{
		fixSurfaceDesc(desc.dwFlags, desc.ddsCaps.dwCaps);
		HRESULT result = dd->CreateSurface(&dd, &desc, &surface, nullptr);
		if (FAILED(result))
		{
			return result;
		}

		auto surface7(CompatPtr<IDirectDrawSurface7>::from(surface));
		attach(*surface7, std::move(privateData));

		if (desc.ddsCaps.dwCaps & DDSCAPS_COMPLEX)
		{
			auto attachedSurfaces(getAllAttachedSurfaces(*surface7));
			for (std::size_t i = 0; i < attachedSurfaces.size(); ++i)
			{
				attach(*attachedSurfaces[i], std::make_unique<Surface>());
			}
		}

		return result;
	}

	template HRESULT Surface::create(
		CompatRef<IDirectDraw> dd, DDSURFACEDESC desc, IDirectDrawSurface*& surface, std::unique_ptr<Surface> privateData);
	template HRESULT Surface::create(
		CompatRef<IDirectDraw2> dd, DDSURFACEDESC desc, IDirectDrawSurface*& surface, std::unique_ptr<Surface> privateData);
	template HRESULT Surface::create(
		CompatRef<IDirectDraw4> dd, DDSURFACEDESC2 desc, IDirectDrawSurface4*& surface, std::unique_ptr<Surface> privateData);
	template HRESULT Surface::create(
		CompatRef<IDirectDraw7> dd, DDSURFACEDESC2 desc, IDirectDrawSurface7*& surface, std::unique_ptr<Surface> privateData);

	void Surface::createImpl()
	{
		m_impl.reset(new SurfaceImpl<IDirectDrawSurface>());
		m_impl2.reset(new SurfaceImpl<IDirectDrawSurface2>());
		m_impl3.reset(new SurfaceImpl<IDirectDrawSurface3>());
		m_impl4.reset(new SurfaceImpl<IDirectDrawSurface4>());
		m_impl7.reset(new SurfaceImpl<IDirectDrawSurface7>());
	}

	template <>
	SurfaceImpl<IDirectDrawSurface>* Surface::getImpl<IDirectDrawSurface>() const { return m_impl.get(); }
	template <>
	SurfaceImpl<IDirectDrawSurface2>* Surface::getImpl<IDirectDrawSurface2>() const { return m_impl2.get(); }
	template <>
	SurfaceImpl<IDirectDrawSurface3>* Surface::getImpl<IDirectDrawSurface3>() const { return m_impl3.get(); }
	template <>
	SurfaceImpl<IDirectDrawSurface4>* Surface::getImpl<IDirectDrawSurface4>() const { return m_impl4.get(); }
	template <>
	SurfaceImpl<IDirectDrawSurface7>* Surface::getImpl<IDirectDrawSurface7>() const { return m_impl7.get(); }

	template <typename TSurface>
	Surface* Surface::getSurface(TSurface& dds)
	{
		Surface* surface = nullptr;
		DWORD surfacePtrSize = sizeof(surface);

		// This can get called during surface release so a proper QueryInterface would be dangerous
		CompatVtable<IDirectDrawSurface7Vtbl>::s_origVtable.GetPrivateData(
			reinterpret_cast<IDirectDrawSurface7*>(&dds),
			IID_CompatSurfacePrivateData, &surface, &surfacePtrSize);

		return surface;
	}

	template Surface* Surface::getSurface(IDirectDrawSurface& dds);
	template Surface* Surface::getSurface(IDirectDrawSurface2& dds);
	template Surface* Surface::getSurface(IDirectDrawSurface3& dds);
	template Surface* Surface::getSurface(IDirectDrawSurface4& dds);
	template Surface* Surface::getSurface(IDirectDrawSurface7& dds);
}
