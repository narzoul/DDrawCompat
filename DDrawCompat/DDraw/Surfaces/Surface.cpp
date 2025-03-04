#include <set>

#include <initguid.h>

#include <Common/CompatPtr.h>
#include <Config/Settings/CompatFixes.h>
#include <DDraw/DirectDrawSurface.h>
#include <DDraw/Surfaces/Surface.h>
#include <DDraw/Surfaces/SurfaceImpl.h>
#include <Win32/DisplayMode.h>

// {C62D8849-DFAC-4454-A1E8-DA67446426BA}
DEFINE_GUID(IID_CompatSurfacePrivateData,
	0xc62d8849, 0xdfac, 0x4454, 0xa1, 0xe8, 0xda, 0x67, 0x44, 0x64, 0x26, 0xba);

namespace
{
	DDSCAPS2 g_currentSurfaceCaps = {};
	std::set<DDraw::Surface*> g_surfaces;

	void heapFree(void* p)
	{
		HeapFree(GetProcessHeap(), 0, p);
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

	Surface::Surface(DWORD origFlags, DWORD origCaps)
		: m_origFlags(origFlags)
		, m_origCaps(origCaps)
		, m_refCount(0)
		, m_sizeOverride{}
		, m_sysMemBuffer(nullptr, &heapFree)
		, m_isPrimary(false)
	{
		g_surfaces.insert(this);
	}

	Surface::~Surface()
	{
		g_surfaces.erase(this);
	}

	void* Surface::alignBuffer(void* buffer)
	{
		auto p = static_cast<BYTE*>(buffer);
		const DWORD alignmentOffset = Config::compatFixes.get().unalignedsurfaces ? 8 : 0;
		const DWORD mod = reinterpret_cast<DWORD>(p) % ALIGNMENT;
		p = p - mod + alignmentOffset;
		if (mod > alignmentOffset)
		{
			p += ALIGNMENT;
		}
		return p;
	}

	void Surface::attach(CompatRef<IDirectDrawSurface7> dds, std::unique_ptr<Surface> privateData)
	{
		if (SUCCEEDED(dds->SetPrivateData(&dds, IID_CompatSurfacePrivateData,
			privateData.get(), sizeof(privateData.get()), DDSPD_IUNKNOWNPOINTER)))
		{
			privateData->m_surface = &dds;
			privateData->createImpl();
			privateData.release();
		}
	}

	template <typename TDirectDraw, typename TSurface, typename TSurfaceDesc>
	HRESULT Surface::create(
		CompatRef<TDirectDraw> dd, TSurfaceDesc desc, TSurface*& surface, std::unique_ptr<Surface> privateData)
	{
		if ((desc.ddsCaps.dwCaps & DDSCAPS_3DDEVICE) &&
			((desc.dwFlags & DDSD_PIXELFORMAT)
				? (desc.ddpfPixelFormat.dwFlags & DDPF_PALETTEINDEXED8)
				: (Win32::DisplayMode::getBpp() <= 8)))
		{
			desc.ddsCaps.dwCaps &= ~DDSCAPS_3DDEVICE;
		}

		if ((desc.dwFlags & DDSD_MIPMAPCOUNT) && 1 == desc.dwMipMapCount)
		{
			desc.dwFlags &= ~DDSD_MIPMAPCOUNT;
			desc.ddsCaps.dwCaps &= ~(DDSCAPS_COMPLEX | DDSCAPS_MIPMAP);
		}

		memcpy(&g_currentSurfaceCaps, &desc.ddsCaps, sizeof(desc.ddsCaps));
		HRESULT result = dd->CreateSurface(&dd, &desc, &surface, nullptr);
		g_currentSurfaceCaps = {};
		if (FAILED(result))
		{
			return result;
		}

		auto surface7(CompatPtr<IDirectDrawSurface7>::from(surface));
		if (desc.ddsCaps.dwCaps & DDSCAPS_COMPLEX)
		{
			auto attachedSurfaces(DirectDrawSurface::getAllAttachedSurfaces(*surface7));
			for (DWORD i = 0; i < attachedSurfaces.size(); ++i)
			{
				attach(*attachedSurfaces[i], std::make_unique<Surface>(privateData->m_origFlags, privateData->m_origCaps));
			}
		}
		else if ((desc.ddsCaps.dwCaps & DDSCAPS_SYSTEMMEMORY) && !(desc.dwFlags & DDSD_LPSURFACE))
		{
			privateData->fixAlignment(*surface7);
		}

		attach(*surface7, std::move(privateData));

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

	void Surface::enumSurfaces(const std::function<void(Surface&)>& callback)
	{
		for (auto surface : g_surfaces)
		{
			callback(*surface);
		}
	}

	void Surface::fixAlignment(CompatRef<IDirectDrawSurface7> surface)
	{
		DDSURFACEDESC2 desc = {};
		desc.dwSize = sizeof(desc);
		if (FAILED(surface->Lock(&surface, nullptr, &desc, DDLOCK_WAIT, nullptr)))
		{
			return;
		}
		surface->Unlock(&surface, nullptr);

		const DWORD alignmentOffset = Config::compatFixes.get().unalignedsurfaces ? 8 : 0;
		const DWORD size = desc.lPitch * desc.dwHeight;
		if (0 == size || alignmentOffset == reinterpret_cast<DWORD>(desc.lpSurface) % ALIGNMENT)
		{
			return;
		}

		m_sysMemBuffer.reset(HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, size + ALIGNMENT));
		if (!m_sysMemBuffer)
		{
			return;
		}

		desc = {};
		desc.dwSize = sizeof(desc);
		desc.dwFlags = DDSD_LPSURFACE;
		desc.lpSurface = alignBuffer(m_sysMemBuffer.get());
		if (FAILED(surface->SetSurfaceDesc(&surface, &desc, 0)))
		{
			m_sysMemBuffer.reset();
		}
	}

	DDSCAPS2 Surface::getCurrentSurfaceCaps()
	{
		return g_currentSurfaceCaps;
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

	void Surface::restore()
	{
	}

	void Surface::setSizeOverride(DWORD width, DWORD height)
	{
		m_sizeOverride.cx = width;
		m_sizeOverride.cy = height;
	}
}
