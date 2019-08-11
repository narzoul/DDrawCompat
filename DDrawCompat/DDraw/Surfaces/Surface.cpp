#include <initguid.h>

#include "Common/CompatPtr.h"
#include "D3dDdi/Device.h"
#include "D3dDdi/Resource.h"
#include "DDraw/DirectDrawSurface.h"
#include "DDraw/Surfaces/Surface.h"
#include "DDraw/Surfaces/SurfaceImpl.h"

// {C62D8849-DFAC-4454-A1E8-DA67446426BA}
DEFINE_GUID(IID_CompatSurfacePrivateData,
	0xc62d8849, 0xdfac, 0x4454, 0xa1, 0xe8, 0xda, 0x67, 0x44, 0x64, 0x26, 0xba);

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

	Surface::Surface(Surface* rootSurface)
		: m_refCount(0)
		, m_rootSurface(rootSurface ? rootSurface : this)
	{
	}

	Surface::~Surface()
	{
		clearResources();
		if (m_rootSurface != this)
		{
			auto it = std::find(m_rootSurface->m_attachedSurfaces.begin(),
				m_rootSurface->m_attachedSurfaces.end(), this);
			if (it != m_rootSurface->m_attachedSurfaces.end())
			{
				m_rootSurface->m_attachedSurfaces.erase(it);
			}

			if (m_rootSurface->m_lockSurface == m_surface)
			{
				m_rootSurface->m_lockSurface.detach();
				m_rootSurface->m_attachedLockSurfaces.clear();
			}
		}
		else
		{
			for (auto attachedSurface : m_attachedSurfaces)
			{
				attachedSurface->m_rootSurface = attachedSurface;
			}

			if (m_lockSurface)
			{
				auto lockSurface(getSurface(*m_lockSurface));
				if (lockSurface)
				{
					lockSurface->m_rootSurface = lockSurface;
				}
			}
		}
	}

	void Surface::attach(CompatRef<IDirectDrawSurface7> dds, std::unique_ptr<Surface> privateData)
	{
		if (SUCCEEDED(dds->SetPrivateData(&dds, IID_CompatSurfacePrivateData,
			privateData.get(), sizeof(privateData.get()), DDSPD_IUNKNOWNPOINTER)))
		{
			privateData->createImpl();
			privateData->m_surface = &dds;
			privateData.release();
		}
	}

	void Surface::clearResources()
	{
		if (!m_surface)
		{
			return;
		}

		auto resource = D3dDdi::Device::getResource(getDriverResourceHandle(*m_surface));
		if (resource)
		{
			resource->setLockResource(nullptr);
			resource->setRootSurface(nullptr);
		}

		for (auto attachedSurface : m_attachedSurfaces)
		{
			attachedSurface->clearResources();
		}
	}

	template <typename TDirectDraw, typename TSurface, typename TSurfaceDesc>
	HRESULT Surface::create(
		CompatRef<TDirectDraw> dd, TSurfaceDesc desc, TSurface*& surface, std::unique_ptr<Surface> privateData)
	{
		HRESULT result = dd->CreateSurface(&dd, &desc, &surface, nullptr);
		if (FAILED(result))
		{
			return result;
		}

		auto surface7(CompatPtr<IDirectDrawSurface7>::from(surface));
		if (!(desc.dwFlags & DDSD_PIXELFORMAT))
		{
			desc.dwFlags |= DDSD_PIXELFORMAT;
			desc.ddpfPixelFormat = {};
			desc.ddpfPixelFormat.dwSize = sizeof(desc.ddpfPixelFormat);
			surface7->GetPixelFormat(surface7, &desc.ddpfPixelFormat);
		}

		privateData->m_lockSurface = privateData->createLockSurface<TDirectDraw, TSurface>(dd, desc);
		if (privateData->m_lockSurface)
		{
			attach(*privateData->m_lockSurface, std::make_unique<Surface>(privateData.get()));
		}

		if (desc.ddsCaps.dwCaps & DDSCAPS_COMPLEX)
		{
			auto attachedSurfaces(getAllAttachedSurfaces(*surface7));
			if (privateData->m_lockSurface)
			{
				auto attachedLockSurfaces(getAllAttachedSurfaces(*privateData->m_lockSurface));
				privateData->m_attachedLockSurfaces.assign(attachedLockSurfaces.begin(), attachedLockSurfaces.end());
			}

			for (DWORD i = 0; i < attachedSurfaces.size(); ++i)
			{
				auto data(std::make_unique<Surface>(privateData.get()));
				privateData->m_attachedSurfaces.push_back(data.get());
				attach(*attachedSurfaces[i], std::move(data));
			}
		}

		Surface* rootSurface = privateData.get();
		attach(*surface7, std::move(privateData));
		rootSurface->restore();

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
		m_impl.reset(new SurfaceImpl<IDirectDrawSurface>(this));
		m_impl2.reset(new SurfaceImpl<IDirectDrawSurface2>(this));
		m_impl3.reset(new SurfaceImpl<IDirectDrawSurface3>(this));
		m_impl4.reset(new SurfaceImpl<IDirectDrawSurface4>(this));
		m_impl7.reset(new SurfaceImpl<IDirectDrawSurface7>(this));
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

	template <typename TDirectDraw, typename TSurface, typename TSurfaceDesc>
	CompatPtr<IDirectDrawSurface7> Surface::createLockSurface(CompatRef<TDirectDraw> dd, TSurfaceDesc desc)
	{
		LOG_FUNC("Surface::createLockSurface", dd, desc);

		if ((desc.ddsCaps.dwCaps & DDSCAPS_SYSTEMMEMORY) ||
			!(desc.ddpfPixelFormat.dwFlags & DDPF_RGB) ||
			0 == desc.ddpfPixelFormat.dwRGBBitCount ||
			desc.ddpfPixelFormat.dwRGBBitCount > 32 ||
			0 != (desc.ddpfPixelFormat.dwRGBBitCount % 8))
		{
			return LOG_RESULT(nullptr);
		}

		desc.ddsCaps.dwCaps &= ~(DDSCAPS_VIDEOMEMORY | DDSCAPS_LOCALVIDMEM | DDSCAPS_NONLOCALVIDMEM);
		desc.ddsCaps.dwCaps |= DDSCAPS_SYSTEMMEMORY;

		CompatPtr<TSurface> lockSurface;
		dd->CreateSurface(&dd, &desc, &lockSurface.getRef(), nullptr);
		return LOG_RESULT(lockSurface);
	}

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

	void Surface::restore()
	{
		setResources(m_lockSurface);
		if (m_lockSurface)
		{
			for (std::size_t i = 0; i < m_attachedSurfaces.size(); ++i)
			{
				m_attachedSurfaces[i]->setResources(
					i < m_attachedLockSurfaces.size() ? m_attachedLockSurfaces[i] : nullptr);
			}
		}
	}

	void Surface::setResources(CompatWeakPtr<IDirectDrawSurface7> lockSurface)
	{
		if (lockSurface)
		{
			auto resource = D3dDdi::Device::getResource(getDriverResourceHandle(*m_surface));
			if (resource)
			{
				resource->setLockResource(D3dDdi::Device::getResource(getDriverResourceHandle(*lockSurface)));
				resource->setRootSurface(m_rootSurface);
			}
		}
	}
}
